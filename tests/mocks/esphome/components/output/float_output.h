#pragma once
namespace esphome::output {
class FloatOutput {
 public:
  virtual ~FloatOutput() = default;
  virtual void set_level(float value) { level = value; }
  float level{0.0f};
};
}
