#pragma once

// Host-test shim: on the device this pulls in IPAddress alongside the WiFi
// stack; here IPAddress lives in the WiFiUdp mock.
#include "WiFiUdp.h"
