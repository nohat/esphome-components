#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Minimal stub for Arduino's IPAddress.
//
// fromString() does real dotted-quad validation, so tests of the target
// parser exercise the same accept/reject behaviour as the device.
class IPAddress {
 public:
  IPAddress() = default;
  explicit IPAddress(const char* ip_str) { fromString(ip_str); }

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

// Minimal stub for WiFiUDP.
class WiFiUDP {
 public:
  WiFiUDP() = default;
  void begin(int port) { port_ = port; }
  void beginPacket(const IPAddress& ip, int port) { current_ = ip; }
  void write(const uint8_t* data, size_t len) { /* stub */ }
  void endPacket() { packets_.push_back(current_); }

  int get_port() const { return port_; }
  const std::vector<IPAddress>& packets() const { return packets_; }
  void clear_packets() { packets_.clear(); }

 private:
  int port_{0};
  IPAddress current_;
  std::vector<IPAddress> packets_;
};
