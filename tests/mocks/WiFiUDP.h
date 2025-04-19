#pragma once

#include <string>

// Minimal stub for IPAddress
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(const char* ip_str) : ip_(ip_str) {}
  bool fromString(const char* ip_str) { 
      // For simplicity, reject an empty string.
      if (!ip_str || ip_str[0] == '\0') return false;
      ip_ = ip_str; 
      return true; 
  }
  std::string toString() const { return ip_; }
 private:
  std::string ip_;
};

// Minimal stub for WiFiUDP
class WiFiUDP {
 public:
  WiFiUDP() = default;
  void begin(int port) { port_ = port; }
  void beginPacket(const IPAddress& ip, int port) { /* stub */ }
  void write(const uint8_t* data, size_t len) { /* stub */ }
  void endPacket() { /* stub */ }
  int get_port() const { return port_; }
 private:
  int port_{0};
};