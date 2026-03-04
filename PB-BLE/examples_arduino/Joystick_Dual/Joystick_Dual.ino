#include "PBGamepad.h"
#include "PBJoystick.h"

void setup() {
  Serial.begin(115200);
  // Start BLE with the same name used in the app.
  // ตั้งชื่อ BLE ให้ตรงกับในแอป
  PBGamepad_init("PB-01");
}

void loop() {
  // Update joystick axes before reading.
  // อัปเดตแกนจอยก่อนอ่านค่า
  PB_JoystickDual_updateAxes();
  int lx = PB_JoystickDual_getLX100();
  int ly = PB_JoystickDual_getLY100();
  int rx = PB_JoystickDual_getRX100();
  int ry = PB_JoystickDual_getRY100();

  // Read buttons if needed.
  // อ่านปุ่มได้เหมือนกัน
  uint8_t btn = PB_GetButtonsLow();

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

  // Use lx/ly/rx/ry to drive motors.
  // ใช้ค่า lx/ly/rx/ry ไปคุมมอเตอร์
  (void)lx;
  (void)ly;
  (void)rx;
  (void)ry;
  (void)btn;
}
