#pragma once
#include "esphome/core/component.h" // Use the correct Component definition
#include "esphome/core/log.h"       // For ESP_LOGD and ESP_LOGW
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <vector>

namespace wiz_client
{

  class WizClient : public esphome::Component
  {
  public:
    WizClient() : brightness_(100) {}

    void setup() override
    {
      ESP_LOGD("wiz", "Initializing WizClient UDP on port 38899");
      udp_.begin(38899);
    }

    void add_bulb(const char *ip_str)
    {
      IPAddress ip;
      if (ip.fromString(ip_str))
      {
        ips_.push_back(ip);
        ESP_LOGD("wiz", "Added Wiz bulb %s", ip_str);
      }
      else
      {
        ESP_LOGW("wiz", "Invalid IP for Wiz bulb: %s", ip_str);
      }
    }

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
