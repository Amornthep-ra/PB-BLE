#pragma once

// Set to 1 for generic pins; 0 for PB pins.
// Default stays on PB hardware mapping for the Arduino examples.
// ESP32-C3/PicoMini users: set LFR_USE_GENERIC_HW=1 and edit LineFollowerRobotHW.cpp pins before uploading.
#ifndef LFR_USE_GENERIC_HW
#define LFR_USE_GENERIC_HW 0
#endif
