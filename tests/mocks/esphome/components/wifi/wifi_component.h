#pragma once
namespace esphome::wifi {
class WiFiComponent {
 public:
  bool is_connected() const { return connected; }
  bool connected{false};
};
inline WiFiComponent *global_wifi_component = nullptr;
}
