#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// Minimal stub for Arduino's IPAddress.
//
// fromString() does real dotted-quad validation, so tests of the target
// parser exercise the same accept/reject behaviour as the device. The
// uint32_t conversion matches Arduino's little-endian union layout, so
// broadcast-address arithmetic (ip | ~mask) comes out right.
class IPAddress {
 public:
  IPAddress() = default;
  explicit IPAddress(const char* ip_str) { fromString(ip_str); }
  explicit IPAddress(uint32_t raw) {
    for (int i = 0; i < 4; ++i) octets_[i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
    valid_ = true;
  }

  bool fromString(const char* ip_str) {
    if (ip_str == nullptr) return false;
    uint8_t octets[4] = {0, 0, 0, 0};
    int octet = 0;
    int digits = 0;
    int value = 0;
    for (const char* p = ip_str;; ++p) {
      if (*p >= '0' && *p <= '9') {
        if (++digits > 3) return false;
        value = value * 10 + (*p - '0');
        if (value > 255) return false;
      } else if (*p == '.' || *p == '\0') {
        if (digits == 0) return false;
        if (octet > 3) return false;
        octets[octet++] = static_cast<uint8_t>(value);
        digits = 0;
        value = 0;
        if (*p == '\0') break;
      } else {
        return false;
      }
    }
    if (octet != 4) return false;
    for (int i = 0; i < 4; ++i) octets_[i] = octets[i];
    valid_ = true;
    return true;
  }

  std::string toString() const {
    if (!valid_) return std::string();
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", octets_[0], octets_[1], octets_[2], octets_[3]);
    return std::string(buf);
  }

  operator uint32_t() const {
    uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) raw |= static_cast<uint32_t>(octets_[i]) << (8 * i);
    return raw;
  }

  bool operator==(const IPAddress& other) const {
    if (valid_ != other.valid_) return false;
    for (int i = 0; i < 4; ++i)
      if (octets_[i] != other.octets_[i]) return false;
    return true;
  }
  bool operator!=(const IPAddress& other) const { return !(*this == other); }

 private:
  uint8_t octets_[4] = {0, 0, 0, 0};
  bool valid_{false};
};

// Minimal stub for WiFiUDP. Outgoing packets are logged; incoming ones are
// injected by the test via inject().
class WiFiUDP {
 public:
  struct Sent {
    IPAddress to;
    std::string payload;
  };
  struct Incoming {
    IPAddress from;
    std::string payload;
  };

  WiFiUDP() = default;

  void begin(int port) { port_ = port; }

  void beginPacket(const IPAddress& ip, int port) {
    pending_to_ = ip;
    pending_payload_.clear();
  }
  void write(const uint8_t* data, size_t len) {
    pending_payload_.append(reinterpret_cast<const char*>(data), len);
  }
  void endPacket() { sent_.push_back({pending_to_, pending_payload_}); }

  int parsePacket() {
    if (incoming_.empty()) return 0;
    current_ = incoming_.front();
    incoming_.pop_front();
    return static_cast<int>(current_.payload.size());
  }
  int read(char* buf, size_t max_len) {
    size_t n = current_.payload.size() < max_len ? current_.payload.size() : max_len;
    memcpy(buf, current_.payload.data(), n);
    return static_cast<int>(n);
  }
  IPAddress remoteIP() const { return current_.from; }

  // --- test helpers ---
  int get_port() const { return port_; }
  const std::vector<Sent>& sent() const { return sent_; }
  void clear_sent() { sent_.clear(); }
  void inject(const IPAddress& from, const std::string& payload) {
    incoming_.push_back({from, payload});
  }

 private:
  int port_{0};
  IPAddress pending_to_;
  std::string pending_payload_;
  std::vector<Sent> sent_;
  std::deque<Incoming> incoming_;
  Incoming current_;
};
