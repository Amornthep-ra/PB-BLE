#include "PBGamepad.h"

void setup() {
  Serial.begin(115200);
  // Start BLE with the same name used in the app.
  // ตั้งชื่อ BLE ให้ตรงกับในแอป
  PBGamepad_init("PB-01");
}

void loop() {
  // Read buttons + sliders (DRV/TRN) from the app.
  // อ่านปุ่ม + สไลด์ DRV/TRN จากแอป
  uint8_t btn = PB_GetButtonsLow();
  int drv = PB_GetDriveSpeed();
  int trn = PB_GetTurnSpeed();

  // Combo detection (two buttons). Use bit AND to check both.
  // กดพร้อมกัน 2 ปุ่ม ใช้ bit AND เช็คทั้งสองปุ่ม
  if ((btn & 0x01) && (btn & 0x04)) {
    // Up + Left
  } else if (btn & 0x01) {
    // Up
  } else if (btn & 0x02) {
    // Down
  } else if (btn & 0x04) {
    // Left
  } else if (btn & 0x08) {
    // Right
  }

  // Face buttons (Triangle/Cross/Square/Circle)
  // ปุ่มด้านขวา (Triangle/Cross/Square/Circle)
  if (btn & 0x10) {
    // Triangle
  } else if (btn & 0x20) {
    // Cross
  } else if (btn & 0x40) {
    // Square
  } else if (btn & 0x80) {
    // Circle
  }

  // Use drv/trn to drive motors here.
  // นำ drv/trn ไปคุมมอเตอร์ได้ตรงนี้
  (void)drv;
  (void)trn;
}
