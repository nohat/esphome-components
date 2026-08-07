#pragma once

// Host-test shim: on the device this pulls in IPAddress alongside the WiFi
// stack; here IPAddress lives in the WiFiUdp mock.
#include "WiFiUdp.h"

#include <cstdint>

#define WL_CONNECTED 3

// Fake monotonic clock. Host tests drive it explicitly so discovery timing is
// deterministic instead of depending on wall-clock time.
extern uint32_t mock_millis_value;
inline uint32_t millis() { return mock_millis_value; }

class MockWiFiClass {
 public:
  int status() const { return status_; }
  IPAddress localIP() const { return local_ip_; }
  IPAddress subnetMask() const { return subnet_mask_; }

  void set_status(int s) { status_ = s; }
  void set_network(const char* ip, const char* mask) {
    local_ip_.fromString(ip);
    subnet_mask_.fromString(mask);
  }

 private:
  int status_{WL_CONNECTED};
  IPAddress local_ip_{"192.168.1.106"};
  IPAddress subnet_mask_{"255.255.255.0"};
};

extern MockWiFiClass WiFi;
