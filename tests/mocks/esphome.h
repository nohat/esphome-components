#pragma once

#include <cstdio>
#include <cstdlib>

#define ESP_LOGD(tag, fmt, ...) printf("[D] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] " fmt "\n", ##__VA_ARGS__)

// Minimal stub for Component base class.
class Component {
 public:
  virtual void setup() {}
};