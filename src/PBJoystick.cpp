#include "PBJoystick.h"
#include <cmath>
#include <algorithm>
static PBGamepadAxes axes = {};
void PB_JoystickDual_updateAxes() {
    PBGamepad_poll();
    PBGamepad_readAxes(&axes);
}
float PB_JoystickDual_getLX() { return PBGamepad_isControlFresh() ? axes.lx : 0; }
float PB_JoystickDual_getLY() { return PBGamepad_isControlFresh() ? axes.ly : 0; }
float PB_JoystickDual_getRX() { return PBGamepad_isControlFresh() ? axes.rx : 0; }
float PB_JoystickDual_getRY() { return PBGamepad_isControlFresh() ? axes.ry : 0; }
int PB_JoystickDual_getLX100() { return int(lroundf(PB_JoystickDual_getLX()*100)); }
int PB_JoystickDual_getLY100() { return int(lroundf(PB_JoystickDual_getLY()*100)); }
int PB_JoystickDual_getRX100() { return int(lroundf(PB_JoystickDual_getRX()*100)); }
int PB_JoystickDual_getRY100() { return int(lroundf(PB_JoystickDual_getRY()*100)); }
float PB_BLE_ApplyDeadzone(float value, float threshold) {
    return fabsf(value) <= fabsf(threshold) ? 0 : value;
}
static int mix(float forward, float turn, bool left) {
    if (!std::isfinite(forward) || !std::isfinite(turn)) return 0;
    float l = forward + turn, r = forward - turn;
    float divisor = std::max(1.0f, std::max(fabsf(l), fabsf(r)));
    return int(lroundf((left ? l : r) / divisor * 100));
}
int PB_BLE_ArcadeMixLeft(float forward, float turn) { return mix(forward, turn, true); }
int PB_BLE_ArcadeMixRight(float forward, float turn) { return mix(forward, turn, false); }
int PB_BLE_LimitSpeed(int value, int minimum, int maximum) {
    minimum = PB_Clamp100(minimum); maximum = PB_Clamp100(maximum);
    if (minimum > maximum) std::swap(minimum, maximum);
    if (!value) return 0;
    int magnitude = value > 100 || value < -100 ? 100 : abs(value);
    return (value < 0 ? -1 : 1) * std::max(minimum, std::min(maximum, magnitude));
}

