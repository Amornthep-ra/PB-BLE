#include <PBGamepad.h>
#include <atomic>
static std::atomic<bool> stopRequested(false);
void onStop() {
  // Called from the BLE worker, NOT an ISR. Never block or call BLE APIs here.
  // Set a flag for loop(); immediate hardware stopping requires a thread-safe driver.
  stopRequested.store(true);
}
void setup() {
  Serial.begin(115200);
  // Put your hardware in a safe stopped state before starting BLE.
  PBGamepad_setEmergencyStopCallback(onStop);
  PBGamepad_init("PB-Robot", "Robot01", "ChangeMe123");
}
void loop() {
  PBGamepad_poll();
  if (stopRequested.exchange(false) || !PBGamepad_isControlFresh()) {
    // Stop your actuators here; keep loop non-blocking to service this promptly.
  } else {
    const uint8_t cmd = PB_GetButtonsLow();
    // Implement actions for cmd. Do not reuse values from an earlier iteration.
    (void)cmd;
  }
  delay(5);
}

