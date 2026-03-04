#pragma once
#include <Arduino.h>

// PB hardware mapping
// Edit pins in .cpp if needed

struct LFRMotorPins {
  uint8_t in1;
  uint8_t in2;
  uint8_t pwm;
  bool invert;
};

// Sensor count
int LFR_HW_sensorCount();
// ADC max value
int LFR_HW_sensorMax();
// true if black line gives higher ADC
bool LFR_HW_sensorBlackHigh();
// Init pins
void LFR_HW_begin();
// Read sensor by index
int LFR_HW_readSensor(int idx);
// Set left/right motor speed (-100..100)
void LFR_HW_setMotor(int left, int right);
// Stop both motors
void LFR_HW_stop();
