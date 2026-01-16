// esp32-hal-log.h (compat shim for non-ESP32 builds)
#pragma once

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include_next "esp32-hal-log.h"
#else
#include <Arduino.h>
#ifndef ARDUHAL_LOG_LEVEL
#define ARDUHAL_LOG_LEVEL 0
#endif
#ifndef ARDUHAL_LOG_LEVEL_DEBUG
#define ARDUHAL_LOG_LEVEL_DEBUG 0
#endif
#define log_v(...) ((void)0)
#define log_d(...) ((void)0)
#define log_i(...) ((void)0)
#define log_w(...) ((void)0)
#define log_e(...) ((void)0)
#endif
