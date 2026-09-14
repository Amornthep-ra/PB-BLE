#include <PBGamepad.h>
void setup() {
  Serial.begin(115200);
  // Public example values: replace before use.
  PBGamepad_init("PB-Robot", "Robot01", "ChangeMe123");
}
void loop() {
  PBGamepad_poll();
  if (!PBGamepad_isControlFresh()) {
    // Stop your actuators here. Zeroing library data does NOT stop hardware.
    delay(10);
    return;
  }
  const uint8_t cmd = PB_GetButtonsLow();
  Serial.printf("CMD=%u Level=%u DRV=%d TRN=%d\n",
    cmd, PB_GetSpeedLevel(), PB_GetDriveSpeed(), PB_GetTurnSpeed());
  if (cmd == 1) { /* Up only: implement your action. */ }
  else if (cmd == 2) { /* Down only. */ }
  else if (cmd == 4) { /* Left only. */ }
  else if (cmd == 8) { /* Right only. */ }
  else if (cmd == 5) { /* Up + Left. */ }
  else { /* Other combination or released: implement safe handling. */ }
  delay(20);
}

