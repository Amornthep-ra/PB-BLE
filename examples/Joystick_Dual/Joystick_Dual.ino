#include <PBJoystick.h>
void setup() {
  Serial.begin(115200);
  PBGamepad_init("PB-Robot", "Robot01", "ChangeMe123");
}
void loop() {
  PB_JoystickDual_updateAxes();
  if (!PBGamepad_isControlFresh()) {
    // Stop your actuators here.
    delay(10);
    return;
  }
  int ly = PB_JoystickDual_getLY100();
  int rx = PB_JoystickDual_getRX100();
  Serial.printf("LX=%d LY=%d RX=%d RY=%d\n",
    PB_JoystickDual_getLX100(), ly, rx, PB_JoystickDual_getRY100());
  if (ly < -15) { /* Stick up; map to your own forward direction. */ }
  else if (ly > 15) { /* Stick down. */ }
  if (rx > 15) { /* Stick right. */ }
  delay(20);
}

