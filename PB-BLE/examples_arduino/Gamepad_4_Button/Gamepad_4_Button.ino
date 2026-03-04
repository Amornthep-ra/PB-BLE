#include "PBGamepad.h"

void setup() {
  Serial.begin(115200);
  // Start BLE with the same name used in the app.
  // ตั้งชื่อ BLE ให้ตรงกับในแอป
  PBGamepad_init("PB-01");
}

void loop() {
  // Read 4-button commands from the app.
  // อ่านคำสั่งปุ่ม 4 ปุ่มจากแอป
  uint8_t btn = PB_GetButtonsLow();
  uint8_t level = PB_GetSpeedLevel();
  int spd = PB_SpeedFromLevel(level);

  if (btn & 0x01) {
    // Up
  } else if (btn & 0x02) {
    // Down
  } else if (btn & 0x04) {
    // Left
  } else if (btn & 0x08) {
    // Right
  }

  // Speed level (Lo/Med/Hi)
  // ระดับความเร็ว (Lo/Med/Hi)
  if (level & 0x01) {
    // Lo
  } else if (level & 0x02) {
    // Med
  } else if (level & 0x04) {
    // Hi
  }

  // Use spd to control speed if needed.
  // นำ spd ไปคุมความเร็วได้
  (void)spd;
}
