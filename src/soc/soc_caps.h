#ifndef __SOC_CAPS_H__
#define __SOC_CAPS_H__
// Minimal stub for non-ESP32 builds that lack soc/soc_caps.h.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include_next "soc/soc_caps.h"
#ifndef SOC_BLE_SUPPORTED
#define SOC_BLE_SUPPORTED 1
#endif
#ifndef SOC_BLE_50_SUPPORTED
#define SOC_BLE_50_SUPPORTED 0
#endif
#endif
// Leave SOC_BLE_SUPPORTED/SOC_BLE_50_SUPPORTED undefined for non-ESP32 builds.
#endif /* __SOC_CAPS_H__ */
