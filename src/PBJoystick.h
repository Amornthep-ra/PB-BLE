#pragma once
#include "PBGamepad.h"
void PB_JoystickDual_updateAxes();
float PB_JoystickDual_getLX();
float PB_JoystickDual_getLY();
float PB_JoystickDual_getRX();
float PB_JoystickDual_getRY();
int PB_JoystickDual_getLX100();
int PB_JoystickDual_getLY100();
int PB_JoystickDual_getRX100();
int PB_JoystickDual_getRY100();
// Pure calculations; never write GPIO or drive motors.
float PB_BLE_ApplyDeadzone(float value, float threshold);
int PB_BLE_ArcadeMixLeft(float forward, float turn);
int PB_BLE_ArcadeMixRight(float forward, float turn);
int PB_BLE_LimitSpeed(int value, int minSpeed, int maxSpeed);

