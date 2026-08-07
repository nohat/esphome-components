#pragma once

// Host-test shim: the parts of esphome::Component that wiz_client relies on.

namespace esphome {

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
};

}  // namespace esphome
