#ifndef PB_JOYSTICK_H
#define PB_JOYSTICK_H

#include <Arduino.h>

void PB_JoystickDual_updateAxes(void);

float PB_JoystickDual_getLX(void);
float PB_JoystickDual_getLY(void);
float PB_JoystickDual_getRX(void);
float PB_JoystickDual_getRY(void);

int PB_JoystickDual_getLX100(void);
int PB_JoystickDual_getLY100(void);
int PB_JoystickDual_getRX100(void);
int PB_JoystickDual_getRY100(void);

// ===== โปรโหมด: tank drive ด้วยบล็อกเดียว =====
void PB_BLE_TankDrive_update(void);
void PB_BLE_TankDrive_setSpeedRange(int minSpeed, int maxSpeed);

// ===== โหมด single-stick: จอยซ้ายบังคับ 2 มอเตอร์ =====
void PB_BLE_SingleStick_setSpeedRange(int minSpeed, int maxSpeed);
void PB_BLE_SingleStick_update(void);

float PB_BLE_ApplyDeadzone(float value, float threshold);
int   PB_BLE_ArcadeMixLeft(float forward, float turn);
int   PB_BLE_ArcadeMixRight(float forward, float turn);
int   PB_BLE_LimitSpeed(int value, int minSpeed, int maxSpeed);

// ===== Tank drive (calc only): user maps motors themselves =====
static bool pb_ble_tankdrive_calc_init = false;
static int pb_ble_tankdrive_leftLevel = 0;
static int pb_ble_tankdrive_rightLevel = 0;
static int pb_ble_tankdrive_left = 0;
static int pb_ble_tankdrive_right = 0;
static int pb_ble_tankdrive_min = 0;
static int pb_ble_tankdrive_max = 100;
static int pb_ble_tankdrive_lastMin = -1;
static int pb_ble_tankdrive_lastMax = -1;

static inline void PB_BLE_TankDrive_calc(int minSpeed, int maxSpeed) {
  if (minSpeed < 0) minSpeed = 0;
  if (minSpeed > 100) minSpeed = 100;
  if (maxSpeed < 0) maxSpeed = 0;
  if (maxSpeed > 100) maxSpeed = 100;
  if (minSpeed > maxSpeed) {
    int tmp = minSpeed;
    minSpeed = maxSpeed;
    maxSpeed = tmp;
  }

  if (!pb_ble_tankdrive_calc_init ||
      minSpeed != pb_ble_tankdrive_lastMin ||
      maxSpeed != pb_ble_tankdrive_lastMax) {
    pb_ble_tankdrive_min = minSpeed;
    pb_ble_tankdrive_max = maxSpeed;
    pb_ble_tankdrive_lastMin = minSpeed;
    pb_ble_tankdrive_lastMax = maxSpeed;
    pb_ble_tankdrive_calc_init = true;
  }

  PB_JoystickDual_updateAxes();

  int leftPower = PB_JoystickDual_getLY100();
  int rightPower = PB_JoystickDual_getRY100();
  const int pb_ble_deadzone = 10;

  if (abs(leftPower) < pb_ble_deadzone) leftPower = 0;
  if (abs(rightPower) < pb_ble_deadzone) rightPower = 0;

  pb_ble_tankdrive_leftLevel =
      (int)(pb_ble_tankdrive_leftLevel + 0.3f * (leftPower - pb_ble_tankdrive_leftLevel));
  pb_ble_tankdrive_rightLevel =
      (int)(pb_ble_tankdrive_rightLevel + 0.3f * (rightPower - pb_ble_tankdrive_rightLevel));

  int leftSign = (pb_ble_tankdrive_leftLevel > 0) ? 1 : (pb_ble_tankdrive_leftLevel < 0 ? -1 : 0);
  int rightSign = (pb_ble_tankdrive_rightLevel > 0) ? 1 : (pb_ble_tankdrive_rightLevel < 0 ? -1 : 0);
  int leftMag = abs(pb_ble_tankdrive_leftLevel);
  int rightMag = abs(pb_ble_tankdrive_rightLevel);

  int leftOut = (leftMag == 0) ? 0 : (leftMag * pb_ble_tankdrive_max + 50) / 100;
  int rightOut = (rightMag == 0) ? 0 : (rightMag * pb_ble_tankdrive_max + 50) / 100;

  if (leftOut != 0 && leftOut < pb_ble_tankdrive_min) leftOut = pb_ble_tankdrive_min;
  if (rightOut != 0 && rightOut < pb_ble_tankdrive_min) rightOut = pb_ble_tankdrive_min;
  if (leftOut > pb_ble_tankdrive_max) leftOut = pb_ble_tankdrive_max;
  if (rightOut > pb_ble_tankdrive_max) rightOut = pb_ble_tankdrive_max;

  pb_ble_tankdrive_left = leftOut * leftSign;
  pb_ble_tankdrive_right = rightOut * rightSign;
}

static inline int PB_BLE_TankDrive_left(void) { return pb_ble_tankdrive_left; }
static inline int PB_BLE_TankDrive_right(void) { return pb_ble_tankdrive_right; }

// ===== Single-stick (arcade) calc only =====
static bool pb_ble_singlestick_calc_init = false;
static int pb_ble_singlestick_leftLevel = 0;
static int pb_ble_singlestick_rightLevel = 0;
static int pb_ble_singlestick_min = 0;
static int pb_ble_singlestick_max = 100;
static int pb_ble_singlestick_lastMin = -1;
static int pb_ble_singlestick_lastMax = -1;

static inline void PB_BLE_SingleStick_calc(int minSpeed, int maxSpeed) {
  if (minSpeed < 0) minSpeed = 0;
  if (minSpeed > 100) minSpeed = 100;
  if (maxSpeed < 0) maxSpeed = 0;
  if (maxSpeed > 100) maxSpeed = 100;
  if (minSpeed > maxSpeed) {
    int tmp = minSpeed;
    minSpeed = maxSpeed;
    maxSpeed = tmp;
  }

  if (!pb_ble_singlestick_calc_init ||
      minSpeed != pb_ble_singlestick_lastMin ||
      maxSpeed != pb_ble_singlestick_lastMax) {
    pb_ble_singlestick_min = minSpeed;
    pb_ble_singlestick_max = maxSpeed;
    pb_ble_singlestick_lastMin = minSpeed;
    pb_ble_singlestick_lastMax = maxSpeed;
    pb_ble_singlestick_calc_init = true;
  }

  PB_JoystickDual_updateAxes();

  int forward = PB_JoystickDual_getLY100(); // -100..100 (ขึ้น = บวก)
  int turn = PB_JoystickDual_getLX100();    // -100..100 (ขวา = บวก)
  const int pb_ble_deadzone = 10;

  if (abs(forward) < pb_ble_deadzone) forward = 0;
  if (abs(turn) < pb_ble_deadzone) turn = 0;

  int leftCmd = forward + turn;
  int rightCmd = forward - turn;
  int maxMag = max(abs(leftCmd), abs(rightCmd));
  if (maxMag > 100) {
    leftCmd = leftCmd * 100 / maxMag;
    rightCmd = rightCmd * 100 / maxMag;
  }

  pb_ble_singlestick_leftLevel =
      (int)(pb_ble_singlestick_leftLevel + 0.3f * (leftCmd - pb_ble_singlestick_leftLevel));
  pb_ble_singlestick_rightLevel =
      (int)(pb_ble_singlestick_rightLevel + 0.3f * (rightCmd - pb_ble_singlestick_rightLevel));

  int leftSign = (pb_ble_singlestick_leftLevel > 0) ? 1 : (pb_ble_singlestick_leftLevel < 0 ? -1 : 0);
  int rightSign = (pb_ble_singlestick_rightLevel > 0) ? 1 : (pb_ble_singlestick_rightLevel < 0 ? -1 : 0);
  int leftMag = abs(pb_ble_singlestick_leftLevel);
  int rightMag = abs(pb_ble_singlestick_rightLevel);

  int leftOut = (leftMag == 0) ? 0 : (leftMag * pb_ble_singlestick_max + 50) / 100;
  int rightOut = (rightMag == 0) ? 0 : (rightMag * pb_ble_singlestick_max + 50) / 100;

  if (leftOut != 0 && leftOut < pb_ble_singlestick_min) leftOut = pb_ble_singlestick_min;
  if (rightOut != 0 && rightOut < pb_ble_singlestick_min) rightOut = pb_ble_singlestick_min;
  if (leftOut > pb_ble_singlestick_max) leftOut = pb_ble_singlestick_max;
  if (rightOut > pb_ble_singlestick_max) rightOut = pb_ble_singlestick_max;

  // reuse tank-drive outputs so users can keep the same left/right blocks
  pb_ble_tankdrive_left = leftOut * leftSign;
  pb_ble_tankdrive_right = rightOut * rightSign;
}

#endif
