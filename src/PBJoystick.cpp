#include <Arduino.h>
#include <math.h>

#include "PBJoystick.h"
#include "PBGamepad.h"

// Use board motor API when available. Allow board-specific mapping via macros.
#if __has_include("PB_BLE_MOTOR_MAP.h")
#include "PB_BLE_MOTOR_MAP.h"
#endif

#ifndef PB_BLE_LEFT_MOTOR_A
#define PB_BLE_LEFT_MOTOR_A 1
#endif
#ifndef PB_BLE_RIGHT_MOTOR_A
#define PB_BLE_RIGHT_MOTOR_A 2
#endif

extern "C" void motor_control(int pin, int speed) __attribute__((weak));
extern "C" void motor(int pin, int speed) __attribute__((weak));

static inline void PB_BLE_motor_single(int pin, int speed) {
    if (motor) {
        motor(pin, speed);
    } else if (motor_control) {
        motor_control(pin, speed);
    }
}

static inline void PB_BLE_driveLeft(int speed) {
    PB_BLE_motor_single(PB_BLE_LEFT_MOTOR_A, speed);
#ifdef PB_BLE_LEFT_MOTOR_B
    PB_BLE_motor_single(PB_BLE_LEFT_MOTOR_B, speed);
#endif
}

static inline void PB_BLE_driveRight(int speed) {
    PB_BLE_motor_single(PB_BLE_RIGHT_MOTOR_A, speed);
#ifdef PB_BLE_RIGHT_MOTOR_B
    PB_BLE_motor_single(PB_BLE_RIGHT_MOTOR_B, speed);
#endif
}

// ============================================================================
//  Internal joystick state
// ============================================================================

static float g_LX = 0.0f;
static float g_LY = 0.0f;
static float g_RX = 0.0f;
static float g_RY = 0.0f;

// ============================================================================
//  Parser : "J:lx,ly;rx,ry" (optional extra stuff after '|' or space)
// ============================================================================

static void PBJD_parseJoystick(String s)
{
    s.trim();
    if (s.length() == 0) {
        return;
    }

    // ถ้ามีอักษรอื่นก่อนหน้า (เช่น "XXX J:...") ให้ตัดมาจาก 'J'
    int jPos = s.indexOf('J');
    if (jPos >= 0) {
        s = s.substring(jPos);
        s.trim();
    }

    // ไม่ใช่แพ็กเก็ตจอย (อาจเป็น BTN: / อย่างอื่น) → ไม่แตะค่าเดิม
    if (!s.startsWith("J:")) {
        return;
    }

    // ตัด "J:"
    s = s.substring(2);
    s.trim();

    // ตัดส่วนเกินหลัง '|' หรือ space ถ้ามี
    int cutPos = s.indexOf('|');
    if (cutPos < 0) {
        cutPos = s.indexOf(' ');
    }
    if (cutPos >= 0) {
        s = s.substring(0, cutPos);
        s.trim();
    }

    // แยกซ้าย/ขวา ด้วย ';'
    String left  = s;
    String right = "";

    int semiPos = s.indexOf(';');
    if (semiPos >= 0) {
        left  = s.substring(0, semiPos);
        right = s.substring(semiPos + 1);
    }

    // -------------------------
    // ซ้าย : LX, LY
    // -------------------------
    left.trim();
    int commaL = left.indexOf(',');
    if (commaL >= 0) {
        String lxStr = left.substring(0, commaL);
        String lyStr = left.substring(commaL + 1);
        lxStr.trim();
        lyStr.trim();
        g_LX = lxStr.toFloat();
        g_LY = lyStr.toFloat();
    }

    // -------------------------
    // ขวา : RX, RY
    // -------------------------
    right.trim();
    if (right.length() > 0) {
        int commaR = right.indexOf(',');
        if (commaR >= 0) {
            String rxStr = right.substring(0, commaR);
            String ryStr = right.substring(commaR + 1);
            rxStr.trim();
            ryStr.trim();
            g_RX = rxStr.toFloat();
            g_RY = ryStr.toFloat();
        }
    }

    // Debug (ถ้าต้องการใช้ค่อยเอาคอมเมนต์ออก)
    // Serial.printf("[PBJD] LX=%.2f LY=%.2f | RX=%.2f RY=%.2f\n",
    //               g_LX, g_LY, g_RX, g_RY);
}

// ============================================================================
//  Public API : update & getters
// ============================================================================

void PB_JoystickDual_updateAxes(void)
{
    if (PBGamepad_hasBinary()) {
        g_LX = PBGamepad_getLX();
        g_LY = PBGamepad_getLY();
        g_RX = PBGamepad_getRX();
        g_RY = PBGamepad_getRY();
        return;
    }

    String cmd = PBGamepad_getCommand();
    cmd.trim();
    if (cmd.length() == 0) {
        return;
    }

    PBJD_parseJoystick(cmd);
}

// float getters (-1.0 .. 1.0)
float PB_JoystickDual_getLX(void) { return g_LX; }
float PB_JoystickDual_getLY(void) { return g_LY; }
float PB_JoystickDual_getRX(void) { return g_RX; }
float PB_JoystickDual_getRY(void) { return g_RY; }

static float clamp1(float v)
{
    if (v >  1.0f) return  1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

// int getters (-100 .. 100)
int PB_JoystickDual_getLX100(void)
{
    float v = clamp1(g_LX) * 100.0f;
    return (int)round(v);
}

int PB_JoystickDual_getLY100(void)
{
    // ใช้ -g_LY เพื่อให้ "ดันขึ้น" เป็นค่าบวก (เดินหน้า)
    float v = clamp1(-g_LY) * 100.0f;
    return (int)round(v);
}

int PB_JoystickDual_getRX100(void)
{
    float v = clamp1(g_RX) * 100.0f;
    return (int)round(v);
}

int PB_JoystickDual_getRY100(void)
{
    // ใช้ -g_RY เพื่อให้ "ดันขึ้น" เป็นค่าบวก (เดินหน้า)
    float v = clamp1(-g_RY) * 100.0f;
    return (int)round(v);
}

// ============================================================================
//  Tank Drive helper (โปรโหมด): ใช้กับบล็อกเดียวใน KB-IDE
// ============================================================================

// state ภายในสำหรับทำ ramp นิ่ม ๆ
static int pb_ble_leftLevel  = 0;   // -100..100 ก่อน scale
static int pb_ble_rightLevel = 0;

// smoothing factor 0.0..1.0 (ยิ่งใกล้ 1 ยิ่งตอบสนองไว)
static const float PB_BLE_RAMP_ALPHA = 0.3f;

// ช่วงความเร็วที่อนุญาต (0..100) จะถูกใช้เป็น “เปอร์เซ็นต์” ส่งเข้า motor()
static int pb_ble_minSpeed = 0;     // ค่าต่ำสุดที่ใช้เมื่อไม่ใช่ 0
static int pb_ble_maxSpeed = 100;   // ค่าสูงสุดที่อนุญาต

// ตั้งช่วงความเร็ว (0..100) ถ้า min > max จะสลับให้เอง
void PB_BLE_TankDrive_setSpeedRange(int minSpeed, int maxSpeed)
{
    // clamp ให้อยู่ใน 0..100
    if (minSpeed < 0)   minSpeed = 0;
    if (minSpeed > 100) minSpeed = 100;
    if (maxSpeed < 0)   maxSpeed = 0;
    if (maxSpeed > 100) maxSpeed = 100;

    // ถ้า min > max ให้สลับ
    if (minSpeed > maxSpeed) {
        int tmp = minSpeed;
        minSpeed = maxSpeed;
        maxSpeed = tmp;
    }

    pb_ble_minSpeed = minSpeed;
    pb_ble_maxSpeed = maxSpeed;
}

// helper : scale ค่าระดับ -100..100 → ตามช่วง [-max..-min],0,[min..max]
static int PBJD_scaleLevelToSpeed(int level)
{
    if (level == 0) {
        return 0;
    }

    int sign = (level > 0) ? 1 : -1;
    int mag  = abs(level);      // 1..100

    // map 1..100 → 1..maxSpeed
    int out = (int)round((float)mag * (float)pb_ble_maxSpeed / 100.0f);
    if (out < pb_ble_minSpeed) {
        out = pb_ble_minSpeed;
    }
    if (out > pb_ble_maxSpeed) {
        out = pb_ble_maxSpeed;
    }

    return sign * out;      // ช่วงประมาณ -maxSpeed..-minSpeed หรือ minSpeed..maxSpeed
}

// ===== ฟังก์ชันหลัก เรียกจากบล็อกเดียวใน KB-IDE =====
// ใช้ left/right joystick (LY/RY) เป็น -100..100 → ขับมอเตอร์ซ้าย/ขวา
void PB_BLE_TankDrive_update(void)
{
    // 1) อ่าน packet BLE → อัปเดตแกน LX/LY/RX/RY
    PB_JoystickDual_updateAxes();

    // 2) อ่านค่า power จากจอย (-100..100)
    int leftPower  = PB_JoystickDual_getLY100();
    int rightPower = PB_JoystickDual_getRY100();

    const int deadzone = 10;

    // 3) dead zone กันสั่น
    if (abs(leftPower) < deadzone) {
        leftPower = 0;
    }
    if (abs(rightPower) < deadzone) {
        rightPower = 0;
    }

    // 4) ramp นิ่ม ๆ เข้าเป้า (ตามนิ้ว แต่ไม่หักศอก)
    pb_ble_leftLevel  = (int)(pb_ble_leftLevel  + PB_BLE_RAMP_ALPHA * (leftPower  - pb_ble_leftLevel));
    pb_ble_rightLevel = (int)(pb_ble_rightLevel + PB_BLE_RAMP_ALPHA * (rightPower - pb_ble_rightLevel));

    // 5) scale ตามช่วง min/max speed แล้วสั่งมอเตอร์จริง
    int outL = PBJD_scaleLevelToSpeed(pb_ble_leftLevel);
    int outR = PBJD_scaleLevelToSpeed(pb_ble_rightLevel);

    // level ช่วง -100..100: + = เดินหน้า, - = ถอย, 0 = หยุด
    PB_BLE_driveLeft(outL);   // มอเตอร์ซ้าย
    PB_BLE_driveRight(outR);  // มอเตอร์ขวา
}

// ============================================================================
//  Single-stick helper : จอยซ้ายควบคุม 2 มอเตอร์ แบบเลี้ยวได้ (arcade drive)
// ============================================================================

// state ภายในสำหรับ ramp นิ่ม ๆ
static int pb_ble_single_leftLevel  = 0;
static int pb_ble_single_rightLevel = 0;

// ช่วงความเร็ว (0..100) สำหรับโหมด single-stick
static int pb_ble_single_minSpeed = 0;
static int pb_ble_single_maxSpeed = 100;

void PB_BLE_SingleStick_setSpeedRange(int minSpeed, int maxSpeed)
{
    if (minSpeed < 0)   minSpeed = 0;
    if (minSpeed > 100) minSpeed = 100;
    if (maxSpeed < 0)   maxSpeed = 0;
    if (maxSpeed > 100) maxSpeed = 100;

    if (minSpeed > maxSpeed) {
        int tmp = minSpeed;
        minSpeed = maxSpeed;
        maxSpeed = tmp;
    }

    pb_ble_single_minSpeed = minSpeed;
    pb_ble_single_maxSpeed = maxSpeed;
}

// scale level -100..100 → [-max..-min],0,[min..max]
static int PBJD_single_scaleLevelToSpeed(int level)
{
    if (level == 0) {
        return 0;
    }

    int sign = (level > 0) ? 1 : -1;
    int mag  = abs(level);  // 1..100

    int out = (int)round((float)mag * (float)pb_ble_single_maxSpeed / 100.0f);
    if (out < pb_ble_single_minSpeed) {
        out = pb_ble_single_minSpeed;
    }
    if (out > pb_ble_single_maxSpeed) {
        out = pb_ble_single_maxSpeed;
    }
    return sign * out;
}

void PB_BLE_SingleStick_update(void)
{
    // 1) อ่านแพ็กเก็ต BLE → อัปเดตแกน LX/LY/RX/RY
    PB_JoystickDual_updateAxes();

    // 2) ใช้เฉพาะจอยซ้าย
    int forward = PB_JoystickDual_getLY100(); // -100..100 (ขึ้น = บวก)
    int turn    = PB_JoystickDual_getLX100(); // -100..100 (ขวา = บวก)

    const int deadzone = 10;

    // 3) deadzone แยกกัน
    if (abs(forward) < deadzone) forward = 0;
    if (abs(turn)    < deadzone) turn    = 0;

    // 4) คำนวณคำสั่งซ้าย/ขวาแบบ arcade drive
    int leftCmd  = forward + turn;
    int rightCmd = forward - turn;

    // normalize ให้ไม่เกิน -100..100
    int maxMag = max(abs(leftCmd), abs(rightCmd));
    if (maxMag > 100) {
        leftCmd  = leftCmd  * 100 / maxMag;
        rightCmd = rightCmd * 100 / maxMag;
    }

    // 5) ramp นิ่ม ๆ
    pb_ble_single_leftLevel  = (int)(pb_ble_single_leftLevel  + PB_BLE_RAMP_ALPHA * (leftCmd  - pb_ble_single_leftLevel));
    pb_ble_single_rightLevel = (int)(pb_ble_single_rightLevel + PB_BLE_RAMP_ALPHA * (rightCmd - pb_ble_single_rightLevel));

    // 6) scale ตามช่วง min/max speed แล้วสั่งมอเตอร์ 1,2
    int outL = PBJD_single_scaleLevelToSpeed(pb_ble_single_leftLevel);
    int outR = PBJD_single_scaleLevelToSpeed(pb_ble_single_rightLevel);

    PB_BLE_driveLeft(outL);  // ซ้าย
    PB_BLE_driveRight(outR); // ขวา
}

int PB_BLE_LimitSpeed(int value, int minSpeed, int maxSpeed)
{
    // บังคับให้อยู่ใน 0..100 ก่อน
    if (minSpeed < 0)   minSpeed = 0;
    if (minSpeed > 100) minSpeed = 100;
    if (maxSpeed < 0)   maxSpeed = 0;
    if (maxSpeed > 100) maxSpeed = 100;

    // ถ้า min > max ให้สลับ
    if (minSpeed > maxSpeed) {
        int tmp = minSpeed;
        minSpeed = maxSpeed;
        maxSpeed = tmp;
    }

    // ถ้าไม่มี max เลย ก็ถือว่าให้หยุด
    if (maxSpeed == 0) {
        return 0;
    }

    // ถ้าคำสั่งเป็น 0 อยู่แล้ว คืน 0 เลย (อย่าดันเป็น min)
    if (value == 0) {
        return 0;
    }

    int sign = (value > 0) ? 1 : -1;
    int mag  = abs(value);   // ความแรง 1..∞

    // บีบไม่ให้เกิน maxSpeed
    if (mag > maxSpeed) {
        mag = maxSpeed;
    }

    // ถ้าไม่ใช่ 0 แต่ต่ำกว่า minSpeed → ดันขึ้นเป็น minSpeed
    if (mag < minSpeed) {
        mag = minSpeed;
    }

    return sign * mag;   // ผลลัพธ์ช่วงประมาณ -max..-min หรือ min..max
}

// ============================================================================
//  Helper functions สำหรับบล็อก apply deadzone / arcade mix
// ============================================================================

// Deadzone: ถ้า |value| < threshold → 0, ไม่งั้นคืนค่าเดิม
float PB_BLE_ApplyDeadzone(float value, float threshold)
{
    if (threshold < 0.0f) {
        threshold = -threshold;
    }
    if (fabsf(value) < threshold) {
        return 0.0f;
    }
    return value;
}

// arcade mix สำหรับล้อซ้าย: left = forward + turn
int PB_BLE_ArcadeMixLeft(float forward, float turn)
{
    float cmd = forward + turn;

    // กันหลุดโหดเกิน (เผื่อไว้ที่ ±200)
    if (cmd >  200.0f) cmd =  200.0f;
    if (cmd < -200.0f) cmd = -200.0f;

    return (int)roundf(cmd);
}

// arcade mix สำหรับล้อขวา: right = forward - turn
int PB_BLE_ArcadeMixRight(float forward, float turn)
{
    float cmd = forward - turn;

    if (cmd >  200.0f) cmd =  200.0f;
    if (cmd < -200.0f) cmd = -200.0f;

    return (int)roundf(cmd);
}



