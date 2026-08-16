#pragma once
#include "esphome/core/component.h" // Use the correct Component definition
#include "esphome/core/log.h"       // For ESP_LOGD and ESP_LOGW
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wiz_client
{

  // Upper bound on the target list, so a malformed string can never grow the
  // vector without limit on a device with ~40 KB of usable heap.
  static const size_t MAX_TARGETS = 32;

  // Cap on the diagnostic status string: Home Assistant rejects text_sensor
  // states longer than 255 characters.
  static const size_t MAX_STATUS_LEN = 255;

  // Bulbs answer a broadcast within a few tens of ms, so a long interval is
  // fine once everything has been found. While something is still unresolved
  // we retry much faster.
  static const uint32_t DISCOVERY_RETRY_MS = 10000;

  // Don't let a burst of replies starve the rest of loop().
  static const int MAX_REPLIES_PER_LOOP = 4;

  // A bulb to send to, identified either by a literal IP or by MAC address.
  //
  // MAC targets are the useful kind: Home Assistant can tell you a WiZ bulb's
  // MAC (it's in the device registry's `connections`) but never its IP, and a
  // MAC survives the DHCP lease changes that silently broke the CA House
  // bedroom dimmer.
  struct Target
  {
    bool by_mac{false};
    uint8_t mac[6]{};
    IPAddress ip;
    bool resolved{false};
  };

  class WizClient : public esphome::Component
  {
  public:
    WizClient() : brightness_(100) {}

    void setup() override
    {
      ESP_LOGD("wiz", "Initializing WizClient UDP on port 38899");
      udp_.begin(38899);
    }

    void loop() override
    {
      read_replies_();
      maybe_discover_();
    }

    void set_discovery(bool enabled) { discovery_enabled_ = enabled; }
    void set_discovery_interval(uint32_t ms) { discovery_interval_ms_ = ms; }

    // ---------------------------------------------------------------------
    // Target list
    //
    // The `bulbs:` YAML key seeds this list at compile time, but it is only a
    // factory default: set_targets() replaces it at runtime, which is what
    // lets the target list be edited from Home Assistant instead of requiring
    // a recompile and an OTA.
    // ---------------------------------------------------------------------

    void clear_targets() { targets_.clear(); }

    size_t target_count() const { return targets_.size(); }

    size_t resolved_count() const
    {
      size_t n = 0;
      for (auto &t : targets_)
        if (t.resolved)
          n++;
      return n;
    }

    // Add a single target, given either an IPv4 address ("192.168.1.169") or
    // a MAC ("cc:40:85:64:07:b0", "cc-40-85-64-07-b0" or "cc40856407b0").
    // Returns false if it was invalid, a duplicate, or would exceed
    // MAX_TARGETS.
    bool add_target(const std::string &token)
    {
      std::string t = trim_(token);
      if (t.empty())
        return false;

      if (targets_.size() >= MAX_TARGETS)
      {
        ESP_LOGW("wiz", "Ignoring WiZ target %s: already at the %u-target limit", t.c_str(),
                 (unsigned) MAX_TARGETS);
        return false;
      }

      Target target;

      // IPv4 first: 123.123.123.123 would also parse as 12 hex digits if the
      // dots were stripped, so the literal form has to win.
      if (target.ip.fromString(t.c_str()))
      {
        target.by_mac = false;
        target.resolved = true;
      }
      else if (parse_mac_(t, target.mac))
      {
        target.by_mac = true;
        target.resolved = false;
      }
      else
      {
        ESP_LOGW("wiz", "Ignoring invalid WiZ target '%s' (expected an IPv4 or MAC address)",
                 t.c_str());
        return false;
      }

      for (auto &existing : targets_)
      {
        if (same_target_(existing, target))
        {
          ESP_LOGD("wiz", "WiZ target %s already present, skipping", t.c_str());
          return false;
        }
      }

      targets_.push_back(target);
      ESP_LOGD("wiz", "Added WiZ target %s", describe_(target).c_str());
      return true;
    }

    // Replace the entire target list from a delimited string. Commas,
    // semicolons and whitespace all separate entries, so both
    // "10.0.0.1,10.0.0.2" and "10.0.0.1 10.0.0.2" work.
    //
    // If the caller passed something non-empty but nothing in it parsed, the
    // previous list is kept rather than silently leaving the dimmer with no
    // targets. An explicitly empty string does clear the list.
    size_t set_targets(const std::string &list)
    {
      std::vector<Target> previous = targets_;
      targets_.clear();

      size_t start = 0;
      while (start < list.size())
      {
        size_t end = list.find_first_of(",; \t\r\n", start);
        if (end == std::string::npos)
          end = list.size();
        if (end > start)
          add_target(list.substr(start, end - start));
        start = end + 1;
      }

      bool had_content = list.find_first_not_of(",; \t\r\n") != std::string::npos;
      if (targets_.empty() && had_content && !previous.empty())
      {
        ESP_LOGW("wiz", "'%s' contained no valid targets; keeping the previous %u", list.c_str(),
                 (unsigned) previous.size());
        targets_ = previous;
        return targets_.size();
      }

      // Carry over any address we had already discovered, so editing the list
      // doesn't force a fresh round of broadcasts for bulbs we already know.
      for (auto &t : targets_)
      {
        if (!t.by_mac || t.resolved)
          continue;
        for (auto &old : previous)
        {
          if (old.by_mac && old.resolved && memcmp(old.mac, t.mac, 6) == 0)
          {
            t.ip = old.ip;
            t.resolved = true;
            break;
          }
        }
      }

      // A newly added MAC should be looked up promptly, not at the next hourly
      // sweep.
      if (has_unresolved_())
        discovered_once_ = false;

      ESP_LOGI("wiz", "WiZ targets now: [%s]", get_status().c_str());
      return targets_.size();
    }

    // Canonical form of the configured list, suitable for typing back in.
    std::string get_targets() const
    {
      std::string out;
      for (auto &t : targets_)
      {
        if (!out.empty())
          out += ',';
        out += spec_(t);
      }
      return out;
    }

    // Diagnostic view: what each target currently resolves to. This can differ
    // from get_targets() -- a MAC that no bulb has answered for shows as
    // unresolved, and is skipped when sending.
    std::string get_status() const
    {
      std::string out;
      for (auto &t : targets_)
      {
        if (!out.empty())
          out += ',';
        out += describe_(t);
        if (out.size() > MAX_STATUS_LEN)
        {
          out.resize(MAX_STATUS_LEN - 3);
          out += "...";
          break;
        }
      }
      return out;
    }

    // Retained for the `bulbs:` codegen path and any existing YAML lambdas.
    void add_bulb(const char *ip_str) { add_target(ip_str == nullptr ? std::string() : std::string(ip_str)); }

    // Ask every bulb on the LAN to identify itself. Safe to call at any time.
    void discover_now()
    {
      discovered_once_ = false;
      last_discovery_ = 0;
    }

    // ---------------------------------------------------------------------
    // Commands
    // ---------------------------------------------------------------------

    void set_brightness(int pct)
    {
      pct = std::max(10, std::min(pct, 100));
      brightness_ = pct;
      char buf[128];
      int len = snprintf(buf, sizeof(buf), "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"dimming\":%d}}", brightness_);
      send_udp(buf, len);
    }

    void set_power(bool on)
    {
      const char* tpl = R"({"method":"setPilot","params":{"state":%s}})";
      char buf[96];
      snprintf(buf, sizeof(buf), tpl, on ? "true" : "false");
      send_udp(buf, strlen(buf));
    }

    // Remember a color temperature without putting a packet on the wire.
    // This is the store a Home Assistant sensor import feeds continuously:
    // a bulb that is OFF must not be woken just because the house's target
    // moved, so the value only reaches a bulb inside set_on() /
    // set_color_temperature().
    void store_color_temperature(int temp)
    {
      color_temp_ = std::max(1700, std::min(temp, 6500));
    }

    void set_color_temperature(int temp)
    {
      store_color_temperature(temp);
      char buf[128];
      int len = snprintf(buf, sizeof(buf),
                         "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":%d,\"dimming\":%d}}",
                         color_temp_, brightness_);
      send_udp(buf, len);
    }

    // Turn on at a brightness WITH the stored color temperature in the same
    // datagram. A brightness-only turn-on lets the bulb restore whatever
    // stale white it last held, and nothing upstream can correct it — this
    // transport is invisible to Home Assistant, so Adaptive Lighting never
    // hears about the turn-on at all (CA bathroom, 2026-08-15). Seeding the
    // stored temp makes the bulb start where the house's curve already is.
    void set_on(int pct)
    {
      pct = std::max(10, std::min(pct, 100));
      brightness_ = pct;
      char buf[160];
      int len = snprintf(buf, sizeof(buf),
                         "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"mode\":\"white\",\"temp\":%d,\"dimming\":%d}}",
                         color_temp_, brightness_);
      send_udp(buf, len);
    }

    // Feed a discovery reply through the resolver. Exposed for host tests;
    // on the device this is driven from read_replies_().
    bool handle_reply(const char *payload, size_t len, const IPAddress &from)
    {
      uint8_t mac[6];
      if (!extract_mac_(payload, len, mac))
        return false;

      bool matched = false;
      for (auto &t : targets_)
      {
        if (!t.by_mac || memcmp(t.mac, mac, 6) != 0)
          continue;
        bool changed = !t.resolved || !(t.ip == from);
        t.ip = from;
        t.resolved = true;
        matched = true;
        if (changed)
          ESP_LOGI("wiz", "Resolved WiZ target %s to %s", hex_mac_(mac).c_str(),
                   from.toString().c_str());
      }
      return matched;
    }

  protected:
    // --- parsing helpers (pure, unit tested) ------------------------------

    static std::string trim_(const std::string &s)
    {
      size_t first = s.find_first_not_of(" \t\r\n");
      if (first == std::string::npos)
        return std::string();
      size_t last = s.find_last_not_of(" \t\r\n");
      return s.substr(first, last - first + 1);
    }

    // Accepts colon- or dash-separated MACs and bare 12-digit hex. Dots are
    // deliberately not accepted as separators so that a dotted quad can never
    // be mistaken for a MAC.
    static bool parse_mac_(const std::string &s, uint8_t out[6])
    {
      char hex[13];
      size_t n = 0;
      for (size_t i = 0; i < s.size(); i++)
      {
        char c = s[i];
        if (c == ':' || c == '-')
          continue;
        if (!isxdigit(static_cast<unsigned char>(c)))
          return false;
        if (n >= 12)
          return false;
        hex[n++] = c;
      }
      if (n != 12)
        return false;
      hex[12] = '\0';

      for (int i = 0; i < 6; i++)
      {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        out[i] = static_cast<uint8_t>(strtoul(byte, nullptr, 16));
      }
      return true;
    }

    // Pull the MAC out of a WiZ getPilot reply, which looks like
    // {"method":"getPilot","result":{"mac":"cc40856407b0","rssi":-60,...}}
    // Scanned by hand rather than with a JSON parser to keep the flash cost
    // of this component near zero.
    static bool extract_mac_(const char *payload, size_t len, uint8_t out[6])
    {
      static const char needle[] = "\"mac\":\"";
      const size_t needle_len = sizeof(needle) - 1;
      if (payload == nullptr || len < needle_len + 12)
        return false;

      for (size_t i = 0; i + needle_len + 12 <= len; i++)
      {
        if (memcmp(payload + i, needle, needle_len) != 0)
          continue;
        return parse_mac_(std::string(payload + i + needle_len, 12), out);
      }
      return false;
    }

    static std::string hex_mac_(const uint8_t mac[6])
    {
      char buf[13];
      snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
               mac[5]);
      return std::string(buf);
    }

    static bool same_target_(const Target &a, const Target &b)
    {
      if (a.by_mac != b.by_mac)
        return false;
      if (a.by_mac)
        return memcmp(a.mac, b.mac, 6) == 0;
      return a.ip == b.ip;
    }

    // What the user would type to get this target back.
    static std::string spec_(const Target &t)
    {
      if (!t.by_mac)
        return t.ip.toString().c_str();
      return hex_mac_(t.mac);
    }

    // What this target currently points at.
    static std::string describe_(const Target &t)
    {
      if (!t.by_mac)
        return t.ip.toString().c_str();
      if (!t.resolved)
        return hex_mac_(t.mac) + "=?";
      return hex_mac_(t.mac) + "=" + t.ip.toString().c_str();
    }

    bool has_unresolved_() const
    {
      for (auto &t : targets_)
        if (t.by_mac && !t.resolved)
          return true;
      return false;
    }

    bool has_mac_targets_() const
    {
      for (auto &t : targets_)
        if (t.by_mac)
          return true;
      return false;
    }

    // --- network ----------------------------------------------------------

    virtual bool network_ready_() { return WiFi.status() == WL_CONNECTED; }

    virtual IPAddress broadcast_address_()
    {
      uint32_t ip = static_cast<uint32_t>(WiFi.localIP());
      uint32_t mask = static_cast<uint32_t>(WiFi.subnetMask());
      return IPAddress(ip | ~mask);
    }

    virtual void broadcast_discovery_()
    {
      static const char msg[] = "{\"method\":\"getPilot\",\"params\":{}}";
      IPAddress bcast = broadcast_address_();
      udp_.beginPacket(bcast, 38899);
      udp_.write(reinterpret_cast<const uint8_t *>(msg), sizeof(msg) - 1);
      udp_.endPacket();
      ESP_LOGD("wiz", "Broadcast WiZ discovery to %s", bcast.toString().c_str());
    }

    void maybe_discover_()
    {
      if (!discovery_enabled_ || !has_mac_targets_())
        return;

      uint32_t interval = has_unresolved_() ? DISCOVERY_RETRY_MS : discovery_interval_ms_;
      uint32_t now = millis();
      if (discovered_once_ && (now - last_discovery_) < interval)
        return;
      if (!network_ready_())
        return;

      broadcast_discovery_();
      last_discovery_ = now;
      discovered_once_ = true;
    }

    void read_replies_()
    {
      char buf[192];
      for (int i = 0; i < MAX_REPLIES_PER_LOOP; i++)
      {
        int size = udp_.parsePacket();
        if (size <= 0)
          return;
        int len = udp_.read(buf, sizeof(buf));
        if (len <= 0)
          continue;
        handle_reply(buf, static_cast<size_t>(len), udp_.remoteIP());
      }
    }

    virtual void send_udp(const char *data, size_t len)
    {
      for (auto &t : targets_)
      {
        if (!t.resolved)
          continue;
        udp_.beginPacket(t.ip, 38899);
        udp_.write((const uint8_t *)data, len);
        udp_.endPacket();
        ESP_LOGD("wiz", "Sent UDP to %s: %s", t.ip.toString().c_str(), data);
      }
    }

    WiFiUDP udp_;
    std::vector<Target> targets_;
    int brightness_;
    int color_temp_{2700};
    bool discovery_enabled_{true};
    uint32_t discovery_interval_ms_{3600000};
    uint32_t last_discovery_{0};
    bool discovered_once_{false};
  };

} // namespace wiz_client
