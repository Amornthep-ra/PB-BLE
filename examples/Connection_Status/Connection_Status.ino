#include <PBGamepad.h>
void setup() {
  Serial.begin(115200);
  // Public example values: choose your own unique Board ID and Pair Code.
  PBGamepad_init("PB-Robot", "Robot01", "ChangeMe123");
}
void loop() {
  PBGamepad_poll();
  Serial.printf("Connected=%d Authenticated=%d Fresh=%d\n",
    PBGamepad_isConnected(), PBGamepad_isAuthenticated(), PBGamepad_isControlFresh());
  delay(250);
}

