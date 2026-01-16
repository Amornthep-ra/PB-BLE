// sdkconfig.h (compat shim for non-ESP32 builds)
#pragma once

// On ESP32/ESP-IDF builds, use the real sdkconfig.h.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include_next "sdkconfig.h"
#else
// Minimal defaults to keep non-ESP32 builds compiling.
// Leave BLE-related macros undefined so BLE code paths stay disabled.
#endif
