#pragma once

// Host-test shim: map ESPHome's logging macros onto printf.
#include <cstdio>

#define ESP_LOGD(tag, fmt, ...) printf("[D][" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][" tag "] " fmt "\n", ##__VA_ARGS__)
