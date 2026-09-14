// PBGamepad.h
#ifndef PB_GAMEPAD_H
#define PB_GAMEPAD_H

#include <Arduino.h>
#if !defined(ARDUINO_ARCH_ESP32)
#error "PB-BLE currently supports Arduino ESP32 and ESP32-C3 only."
#else
#include <sdkconfig.h>
#if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "PB-BLE: select ESP32 or ESP32-C3; this target is not yet supported."
#endif
#endif

static const uint16_t PB_BLE_BTN_UP = 1 << 0;
static const uint16_t PB_BLE_BTN_DOWN = 1 << 1;
static const uint16_t PB_BLE_BTN_LEFT = 1 << 2;
static const uint16_t PB_BLE_BTN_RIGHT = 1 << 3;
static const uint16_t PB_BLE_BTN_TRIANGLE = 1 << 4;
static const uint16_t PB_BLE_BTN_CROSS = 1 << 5;
static const uint16_t PB_BLE_BTN_SQUARE = 1 << 6;
static const uint16_t PB_BLE_BTN_CIRCLE = 1 << 7;
static const uint16_t PB_BLE_BTN_SPEED_LOW = 1 << 8;
static const uint16_t PB_BLE_BTN_SPEED_MID = 1 << 9;
static const uint16_t PB_BLE_BTN_SPEED_HIGH = 1 << 10;

struct PBGamepadAxes {
  float lx;
  float ly;
  float rx;
  float ry;
};

typedef void (*PBGamepadStopCallback)(void);

void PBGamepad_setEmergencyStopCallback(PBGamepadStopCallback callback);
void PBGamepad_init(const char *deviceName, const char *boardId, const char *pairCode);
bool PBGamepad_isAuthenticated(void);
String PBGamepad_getCommand(void);

void PBGamepad_sendLine(const String &msg);
bool PBGamepad_isConnected(void);

bool PBGamepad_isControlFresh(void);
void PBGamepad_resetControlState(void);
uint32_t PBGamepad_lastControlAgeMs(void);
void PBGamepad_poll(void);
uint32_t PBGamepad_lastDisconnectAgeMs(void);
uint32_t PBGamepad_advertiseRetryCount(void);

bool PBGamepad_hasBinary(void);
bool PBGamepad_readAxes(PBGamepadAxes *axes);
float PBGamepad_getLX(void);
float PBGamepad_getLY(void);
float PBGamepad_getRX(void);
float PBGamepad_getRY(void);
uint16_t PBGamepad_getButtons(void);
int PBGamepad_getSpeed(void);

// Helpers for cleaner Arduino code (no behavior change).
static inline uint8_t PB_GetButtonsLow(void) {
  uint16_t v = PBGamepad_getButtons();
  return (uint8_t)(v & 0xFF);
}

static inline uint8_t PB_GetSpeedLevel(void) {
  uint16_t v = PBGamepad_getButtons();
  return (uint8_t)((v >> 8) & 0xFF);
}

static inline int PB_SpeedFromLevel(uint8_t level) {
  if (level & 0x01) return 25;
  if (level & 0x02) return 50;
  if (level & 0x04) return 100;
  return 0;
}

static inline int PB_Clamp100(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return value;
}

static inline bool PB_IsCombo(uint8_t low, uint8_t a, uint8_t b) {
  return ((low & a) != 0) && ((low & b) != 0);
}

static inline int PB_GetDriveSpeed(void) {
  float v = PBGamepad_getLX();
  if (v < 0) v = 0;
  int out = (int)(v * 100.0f + 0.5f);
  return PB_Clamp100(out);
}

static inline int PB_GetTurnSpeed(void) {
  float v = PBGamepad_getRX();
  if (v < 0) v = 0;
  int out = (int)(v * 100.0f + 0.5f);
  return PB_Clamp100(out);
}


#endif


