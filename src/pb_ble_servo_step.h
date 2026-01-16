#pragma once
#include <Arduino.h>

// Helper to step a servo channel with bounds and delay.
inline void PB_BLE_ServoStep(int ch, bool inc, int step, int min_angle, int max_angle, int delay_ms) {
    int current = -1;
    switch (ch) {
        #ifdef _servo1
        case 1: current = servo1.read(); break;
        #endif
        #ifdef _servo2
        case 2: current = servo2.read(); break;
        #endif
        #ifdef _servo3
        case 3: current = servo3.read(); break;
        #endif
        #ifdef _servo4
        case 4: current = servo4.read(); break;
        #endif
        #ifdef _servo5
        case 5: current = servo5.read(); break;
        #endif
        #ifdef _servo6
        case 6: current = servo6.read(); break;
        #endif
        #ifdef _servo7
        case 7: current = servo7.read(); break;
        #endif
        #ifdef _servo8
        case 8: current = servo8.read(); break;
        #endif
        default: break;
    }

    if (current == -1) return;  // Channel not available

    int next = inc ? (current + step) : (current - step);
    if (next > max_angle) next = max_angle;
    if (next < min_angle) next = min_angle;

    servo(ch, next);
    delay(delay_ms);
}
