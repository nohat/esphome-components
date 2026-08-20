#pragma once
#include <cstdint>

extern uint32_t mock_millis_value;
uint32_t millis();

// Host-test shim: the parts of esphome::Component that wiz_client relies on.

namespace esphome {

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual float get_setup_priority() const { return 0.0f; }
};

namespace setup_priority {
static constexpr float HARDWARE = 800.0f;
}

}  // namespace esphome
