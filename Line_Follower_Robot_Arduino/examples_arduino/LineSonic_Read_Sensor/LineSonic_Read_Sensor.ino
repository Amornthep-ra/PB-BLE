/*
  LineSonic Read Sensor (Arduino IDE)
  - PB board: keep default.
    - Other boards: set LFR_USE_GENERIC_HW=1 in LFR_HW_Config.h and edit LineFollowerRobotHW.cpp.
*/
#include "LFR_HW_Config.h"
#include "LineFollowerRobot.h"

#if LFR_USE_GENERIC_HW
#include "LineFollowerRobotHW.h"
#else
#include "PB_LineFollowerRobotHW.h"
#endif

// Read all sensors and reply: SENS=a,b,c,...,SUM=x
void sendSensorValues() {
  int count = LFR_HW_sensorCount();
  if (count <= 0) return;

  int sum = 0;
  String msg = "SENS=";
  for (int i = 0; i < count; i++) {
    int v = LFR_HW_readSensor(i);
    sum += v;
    msg += String(v);
    if (i < count - 1) msg += ",";
  }
  msg += ",SUM=" + String(sum);
  LFR_sendLine(msg);
}

void setup() {
  Serial.begin(115200);
  LFR_HW_begin();
  LFR_begin("PB-01");
}

void loop() {
  LFR_poll();

  String line;
  while (LFR_readLine(line)) {
    // App request: SENS=1
    if (line == "SENS=1") {
      sendSensorValues();
    }
  }
}



