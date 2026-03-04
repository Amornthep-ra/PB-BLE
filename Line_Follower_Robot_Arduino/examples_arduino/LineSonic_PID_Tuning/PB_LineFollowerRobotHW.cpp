#include "LFR_HW_Config.h"

#if !LFR_USE_GENERIC_HW
#include "PB_LineFollowerRobotHW.h"

#ifndef LFR_SENSOR_COUNT
#define LFR_SENSOR_COUNT 5
#endif

// Default PB mapping (adjust if your PB board differs).
static const int LFR_SENSOR_PINS[LFR_SENSOR_COUNT] = {34, 35, 32, 33, 25};
static const LFRMotorPins LFR_LEFT_MOTOR = {26, 27, 14, false};
static const LFRMotorPins LFR_RIGHT_MOTOR = {12, 13, 15, false};

#ifndef LFR_SENSOR_BLACK_HIGH
#define LFR_SENSOR_BLACK_HIGH 0
#endif

#ifndef LFR_PWM_MAX
#define LFR_PWM_MAX 255
#endif

#ifndef LFR_SENSOR_MAX
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#define LFR_SENSOR_MAX 4095
#else
#define LFR_SENSOR_MAX 1023
#endif
#endif

int LFR_HW_sensorCount() { return LFR_SENSOR_COUNT; }
int LFR_HW_sensorMax() { return LFR_SENSOR_MAX; }
bool LFR_HW_sensorBlackHigh() { return LFR_SENSOR_BLACK_HIGH != 0; }

// Init sensor + motor pins
void LFR_HW_begin() {
  for (int i = 0; i < LFR_SENSOR_COUNT; i++) {
    pinMode(LFR_SENSOR_PINS[i], INPUT);
  }

  pinMode(LFR_LEFT_MOTOR.in1, OUTPUT);
  pinMode(LFR_LEFT_MOTOR.in2, OUTPUT);
  pinMode(LFR_LEFT_MOTOR.pwm, OUTPUT);
  pinMode(LFR_RIGHT_MOTOR.in1, OUTPUT);
  pinMode(LFR_RIGHT_MOTOR.in2, OUTPUT);
  pinMode(LFR_RIGHT_MOTOR.pwm, OUTPUT);
  LFR_HW_stop();
}

// Read one sensor
int LFR_HW_readSensor(int idx) {
  if (idx < 0 || idx >= LFR_SENSOR_COUNT) return 0;
  return analogRead(LFR_SENSOR_PINS[idx]);
}

// Internal: apply speed to a motor
static void setMotor(const LFRMotorPins &m, int speed) {
  speed = constrain(speed, -100, 100);
  if (m.invert) speed = -speed;
  bool forward = speed >= 0;
  int pwm = map(abs(speed), 0, 100, 0, LFR_PWM_MAX);
  digitalWrite(m.in1, forward ? HIGH : LOW);
  digitalWrite(m.in2, forward ? LOW : HIGH);
  analogWrite(m.pwm, pwm);
}

// Set left/right motor speed (-100..100)
void LFR_HW_setMotor(int left, int right) {
  setMotor(LFR_LEFT_MOTOR, left);
  setMotor(LFR_RIGHT_MOTOR, right);
}

// Stop both motors
void LFR_HW_stop() {
  LFR_HW_setMotor(0, 0);
}

#endif
