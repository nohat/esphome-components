#pragma once
#include "esphome/core/component.h" // Use the correct Component definition
#include "esphome/core/log.h"       // For ESP_LOGD and ESP_LOGW
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wiz_client
{

  // Upper bound on the target list, so a malformed string can never grow the
  // vector without limit on a device with ~40 KB of usable heap.
  static const size_t MAX_TARGETS = 32;

  class WizClient : public esphome::Component
  {
  public:
    WizClient() : brightness_(100) {}

    void setup() override
    {
      ESP_LOGD("wiz", "Initializing WizClient UDP on port 38899");
      udp_.begin(38899);
    }

    // ---------------------------------------------------------------------
    // Target list
    //
    // The `bulbs:` YAML key seeds this list at compile time, but it is only a
    // factory default: set_targets() replaces it at runtime, which is what
    // lets the target list be edited from Home Assistant instead of requiring
    // a recompile and an OTA.
    // ---------------------------------------------------------------------

    void clear_targets() { ips_.clear(); }

    size_t target_count() const { return ips_.size(); }

    // Add a single IPv4 target. Returns false if it was invalid, a duplicate,
    // or would exceed MAX_TARGETS.
    bool add_target(const std::string &token)
    {
      std::string t = trim_(token);
      if (t.empty())
        return false;

      if (ips_.size() >= MAX_TARGETS)
      {
        ESP_LOGW("wiz", "Ignoring WiZ target %s: already at the %u-target limit", t.c_str(),
                 (unsigned) MAX_TARGETS);
        return false;
      }

      IPAddress ip;
      if (!ip.fromString(t.c_str()))
      {
        ESP_LOGW("wiz", "Ignoring invalid WiZ target '%s' (expected an IPv4 address)", t.c_str());
        return false;
      }

      for (auto &existing : ips_)
      {
        if (existing == ip)
        {
          ESP_LOGD("wiz", "WiZ target %s already present, skipping", t.c_str());
          return false;
        }
      }

      ips_.push_back(ip);
      ESP_LOGD("wiz", "Added WiZ target %s", t.c_str());
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
      std::vector<IPAddress> previous = ips_;
      ips_.clear();

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
      if (ips_.empty() && had_content && !previous.empty())
      {
        ESP_LOGW("wiz", "'%s' contained no valid targets; keeping the previous %u", list.c_str(),
                 (unsigned) previous.size());
        ips_ = previous;
        return ips_.size();
      }

      ESP_LOGI("wiz", "WiZ targets now: [%s]", get_targets().c_str());
      return ips_.size();
    }

    // Comma-separated readback of the list actually in use, for the
    // diagnostic text_sensor in Home Assistant.
    std::string get_targets() const
    {
      std::string out;
      for (auto &ip : ips_)
      {
        if (!out.empty())
          out += ',';
        out += ip.toString().c_str();
      }
      return out;
    }

    // Retained for the `bulbs:` codegen path and any existing YAML lambdas.
    void add_bulb(const char *ip_str) { add_target(ip_str == nullptr ? std::string() : std::string(ip_str)); }

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

    void set_color_temperature(int temp)
    {
      temp = std::max(1700, std::min(temp, 6500));
      char buf[128];
      int len = snprintf(buf, sizeof(buf),
                         "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":%d,\"dimming\":%d}}",
                         temp, brightness_);
      send_udp(buf, len);
    }

  protected:
    static std::string trim_(const std::string &s)
    {
      size_t first = s.find_first_not_of(" \t\r\n");
      if (first == std::string::npos)
        return std::string();
      size_t last = s.find_last_not_of(" \t\r\n");
      return s.substr(first, last - first + 1);
    }

    virtual void send_udp(const char *data, size_t len)
    {
      for (auto &ip : ips_)
      {
        udp_.beginPacket(ip, 38899);
        udp_.write((const uint8_t *)data, len);
        udp_.endPacket();
        ESP_LOGD("wiz", "Sent UDP to %s: %s", ip.toString().c_str(), data);
      }
    }

    WiFiUDP udp_;
    std::vector<IPAddress> ips_;
    int brightness_;
  };

} // namespace wiz_client
