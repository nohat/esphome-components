#pragma once
namespace esphome::api {
class APIServer {
 public:
  bool is_connected_with_state_subscription() const { return connected; }
  bool connected{false};
};
inline APIServer *global_api_server = nullptr;
}
