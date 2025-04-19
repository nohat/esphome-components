#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <WiFiUDP.h>
#include <vector>

class WizClient : public Component {
 public:
  // Initialize with optional starting brightness (0-100)
  WizClient() : brightness_(100) {}

  void setup() override {
    ESP_LOGD("wiz", "Initializing WizClient UDP on port 38899");
    udp_.begin(38899);
  }

  // Add a bulb to control by IP ("192.168.1.42")
  void add_bulb(const char* ip_str) {
    IPAddress ip;
    if (ip.fromString(ip_str)) {
      ips_.push_back(ip);
      ESP_LOGD("wiz", "Added Wiz bulb %s", ip_str);
    } else {
      ESP_LOGW("wiz", "Invalid IP for Wiz bulb: %s", ip_str);
    }
  }

  // Set global brightness (0-100)
  void set_brightness(int brightness) {
    brightness_ = std::max(0, std::min(brightness, 100));
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
      "{\"method\":\"setPilot\",\"params\":{\"brightness\":%d}}",
      brightness_);
    send_udp(buf, len);
  }

  // Set color temperature in Kelvin (1700-6500)
  void set_color_temperature(int temp) {
    temp = std::max(1700, std::min(temp, 6500));
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
      "{\"method\":\"setPilot\",\"params\":{\"mode\":\"white\",\"temp\":%d,\"brightness\":%d}}",
      temp, brightness_);
    send_udp(buf, len);
  }

 protected:
  // Make send_udp virtual so that it can be overridden in tests.
  virtual void send_udp(const char* data, size_t len) {
    for (auto &ip : ips_) {
      udp_.beginPacket(ip, 38899);
      udp_.write((const uint8_t*)data, len);
      udp_.endPacket();
      ESP_LOGD("wiz", "Sent UDP to %s: %s", ip.toString().c_str(), data);
    }
  }

  WiFiUDP udp_;
  std::vector<IPAddress> ips_;
  int brightness_;
};
