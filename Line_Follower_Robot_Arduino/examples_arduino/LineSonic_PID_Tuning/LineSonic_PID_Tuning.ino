/*
  LineSonic PID Tuning (Arduino IDE)
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

// ----- Config (override if needed) -----
// LFR_LOOP_INTERVAL_MS: control loop interval (ms)
// LFR_CHECKSUM_*: tune CHECKSUM mode behavior
#ifndef LFR_LOOP_INTERVAL_MS
#define LFR_LOOP_INTERVAL_MS 10
#endif

#ifndef LFR_CHECKSUM_COUNT_MAX
#define LFR_CHECKSUM_COUNT_MAX 50
#endif

// -1=auto, 0=target sum, 1=line count
#ifndef LFR_CHECKSUM_USE_COUNT
#define LFR_CHECKSUM_USE_COUNT -1
#endif

#ifndef LFR_CHECKSUM_DEBOUNCE_MS
#define LFR_CHECKSUM_DEBOUNCE_MS 120
#endif

// 0=auto (60% of max sum)
#ifndef LFR_DEFAULT_CHECKSUM_THRESHOLD
#define LFR_DEFAULT_CHECKSUM_THRESHOLD 0
#endif

const int LFR_MAX_SENSORS = 8;

struct Step {
  bool isChecksum;
  int value;
  float kp;
  float kd;
  int speed;
  int afterMs;
  int afterSpeed;
  int stopMs;
  int lineColor;     // 0=black, 1=white
  int holdMs;
  int holdThreshold;
  int holdMode;      // 0=off, 1=all black, 2=all white
  bool continueAfter;
  bool stopBetween;
};

struct LineMetrics {
  int sum;
  float error;
  bool valid;
};

const int MAX_STEPS = 16;
Step steps[MAX_STEPS];
int stepCount = 0;

enum RunPhase { RunIdle, RunStep, RunAfter, RunStop };
RunPhase runPhase = RunIdle;
int currentStep = -1;
unsigned long phaseStartMs = 0;
unsigned long stepStartMs = 0;
float lastError = 0.0f;
bool holdActive = false;
unsigned long holdStartMs = 0;
int stepLineCount = 0;
bool stepLineDetected = false;
unsigned long stepLastCrossMs = 0;
unsigned long lastControlMs = 0;

static int sensorRaw[LFR_MAX_SENSORS];
static int sensorCount = 0;

int splitParts(const String &s, char delim, String *out, int maxParts) {
  int count = 0;
  int start = 0;
  while (count < maxParts) {
    int idx = s.indexOf(delim, start);
    if (idx < 0) {
      out[count++] = s.substring(start);
      break;
    }
    out[count++] = s.substring(start, idx);
    start = idx + 1;
  }
  return count;
}

// Reset running state and stop motors
void resetRun() {
  runPhase = RunIdle;
  currentStep = -1;
  holdActive = false;
  stepLineCount = 0;
  stepLineDetected = false;
  LFR_HW_stop();
}

// Prepare a step and reset counters
void beginStep(int index) {
  if (index < 0 || index >= stepCount) {
    resetRun();
    return;
  }
  currentStep = index;
  runPhase = RunStep;
  stepStartMs = millis();
  phaseStartMs = stepStartMs;
  lastError = 0.0f;
  holdActive = false;
  stepLineCount = 0;
  stepLineDetected = false;
  stepLastCrossMs = 0;
}

// Start the step sequence (from step 0)
void startRun() {
  if (stepCount <= 0) return;
  beginStep(0);
}

// Advance to next step
void nextStep() {
  beginStep(currentStep + 1);
}

// Read sensors into sensorRaw[]
int readSensors() {
  int count = LFR_HW_sensorCount();
  if (count > LFR_MAX_SENSORS) count = LFR_MAX_SENSORS;
  for (int i = 0; i < count; i++) {
    sensorRaw[i] = LFR_HW_readSensor(i);
  }
  sensorCount = count;
  return count;
}

// Compute line sum and position error (-100..100)
void computeMetrics(int lineColor, LineMetrics &out) {
  int count = sensorCount;
  if (count <= 0) {
    out.sum = 0;
    out.error = 0.0f;
    out.valid = false;
    return;
  }

  int maxVal = LFR_HW_sensorMax();
  bool blackHigh = LFR_HW_sensorBlackHigh();
  long sum = 0;
  long weighted = 0;

  for (int i = 0; i < count; i++) {
    int raw = sensorRaw[i];
    if (raw < 0) raw = 0;
    if (raw > maxVal) raw = maxVal;

    int lineSignal = 0;
    if (lineColor == 0) { // black line
      lineSignal = blackHigh ? raw : (maxVal - raw);
    } else { // white line
      lineSignal = blackHigh ? (maxVal - raw) : raw;
    }

    sum += lineSignal;
    weighted += (long)lineSignal * (i * 1000);
  }

  out.sum = (int)sum;
  if (sum > 0 && count > 1) {
    float pos = (float)weighted / (float)sum; // 0..(count-1)*1000
    float center = ((count - 1) * 1000.0f) / 2.0f;
    float norm = (pos - center) / center; // -1..1
    out.error = norm * 100.0f;
    out.valid = true;
  } else {
    out.error = 0.0f;
    out.valid = false;
  }
}

// Threshold used for CHECKSUM line detection
int checksumThreshold(const Step &s) {
  if (s.holdThreshold > 0) return s.holdThreshold;
  if (LFR_DEFAULT_CHECKSUM_THRESHOLD > 0) return LFR_DEFAULT_CHECKSUM_THRESHOLD;
  int maxVal = LFR_HW_sensorMax();
  int count = LFR_HW_sensorCount();
  if (count <= 0) count = 1;
  return (maxVal * count * 6) / 10; // 60% of max
}

// Decide CHECKSUM mode: line count vs target sum
bool useChecksumCount(const Step &s) {
  if (LFR_CHECKSUM_USE_COUNT == 1) return true;
  if (LFR_CHECKSUM_USE_COUNT == 0) return false;
  return s.value > 0 && s.value <= LFR_CHECKSUM_COUNT_MAX;
}

// CHECKSUM: decide when condition is met
bool checkChecksum(const Step &s, int lineSum, unsigned long now) {
  if (s.value <= 0) return false;

  if (!useChecksumCount(s)) {
    return lineSum >= s.value;
  }

  int threshold = checksumThreshold(s);
  bool detected = lineSum >= threshold;
  if (detected && !stepLineDetected && (now - stepLastCrossMs) >= LFR_CHECKSUM_DEBOUNCE_MS) {
    stepLineCount++;
    stepLastCrossMs = now;
  }
  stepLineDetected = detected;
  return stepLineCount >= s.value;
}

// Parse full step sequence from app: SEQ=...
void parseSeq(const String &payload) {
  stepCount = 0;
  int start = 0;
  while (stepCount < MAX_STEPS) {
    int idx = payload.indexOf(';', start);
    String stepStr = (idx < 0) ? payload.substring(start) : payload.substring(start, idx);
    stepStr.trim();
    if (stepStr.length() > 0) {
      String f[12];
      int n = splitParts(stepStr, ',', f, 12);
      Step s;
      String mode = (n > 0) ? f[0] : "TIME";
      mode.trim();
      s.isChecksum = (mode == "CHECKSUM");
      s.value = (n > 1) ? f[1].toInt() : 0;
      s.kp = (n > 2) ? f[2].toFloat() : 0.0f;
      s.kd = (n > 3) ? f[3].toFloat() : 0.0f;
      s.speed = (n > 4) ? f[4].toInt() : 0;
      s.afterMs = (n > 5) ? f[5].toInt() : 0;
      s.afterSpeed = (n > 6) ? f[6].toInt() : 0;
      s.stopMs = (n > 7) ? f[7].toInt() : 0;
      s.lineColor = (n > 8) ? f[8].toInt() : 0;
      s.holdMs = (n > 9) ? f[9].toInt() : 0;
      s.holdThreshold = (n > 10) ? f[10].toInt() : 0;
      s.holdMode = (n > 11) ? f[11].toInt() : 0;
      s.continueAfter = (s.afterMs > 0);
      s.stopBetween = (s.stopMs > 0);
      steps[stepCount++] = s;
    }
    if (idx < 0) break;
    start = idx + 1;
  }

  resetRun();
  Serial.print("[SEQ] steps=");
  Serial.println(stepCount);
  LFR_sendLine("ACK:SEQ");
}

// Update KP/KD/Speed only: SEQPD=...
void parseSeqPd(const String &payload) {
  if (stepCount <= 0) {
    Serial.println("[SEQPD] ignored no sequence");
    LFR_sendLine("ERR:NO_SEQ");
    return;
  }

  int idxStep = 0;
  int start = 0;
  while (idxStep < MAX_STEPS) {
    int idx = payload.indexOf(';', start);
    String stepStr = (idx < 0) ? payload.substring(start) : payload.substring(start, idx);
    stepStr.trim();
    if (stepStr.length() > 0) {
      String f[3];
      int n = splitParts(stepStr, ',', f, 3);
      float kp = (n > 0) ? f[0].toFloat() : 0.0f;
      float kd = (n > 1) ? f[1].toFloat() : 0.0f;
      int spd = (n > 2) ? f[2].toInt() : 0;
      if (idxStep < stepCount) {
        steps[idxStep].kp = kp;
        steps[idxStep].kd = kd;
        steps[idxStep].speed = spd;
      }
    }
    if (idx < 0) break;
    start = idx + 1;
    idxStep++;
  }

  Serial.println("[SEQPD] updated KP/KD/Speed");
  LFR_sendLine("ACK:SEQPD");
}

// Main runner: PD control + step timing/checksum
void updateRun() {
  if (runPhase == RunIdle) return;
  if (currentStep < 0 || currentStep >= stepCount) {
    resetRun();
    return;
  }

  Step &s = steps[currentStep];
  unsigned long now = millis();

  if (runPhase == RunStop) {
    if (now - phaseStartMs >= (unsigned long)s.stopMs) {
      nextStep();
    } else {
      LFR_HW_stop();
    }
    return;
  }

  if (now - lastControlMs < (unsigned long)LFR_LOOP_INTERVAL_MS) return;
  lastControlMs = now;

  readSensors();
  LineMetrics line;
  computeMetrics(s.lineColor, line);

  int baseSpeed = (runPhase == RunAfter)
      ? ((s.afterSpeed != 0) ? s.afterSpeed : s.speed)
      : s.speed;

  bool forceStraight = false;
  if (s.holdMode != 0 && s.holdMs > 0) {
    LineMetrics holdLine;
    int holdColor = (s.holdMode == 1) ? 0 : 1;
    computeMetrics(holdColor, holdLine);
    int threshold = checksumThreshold(s);
    bool holdDetected = (holdLine.sum >= threshold);

    if (holdDetected && !holdActive) {
      holdActive = true;
      holdStartMs = now;
    }

    if (holdActive) {
      if (now - holdStartMs < (unsigned long)s.holdMs) {
        forceStraight = true;
      } else {
        holdActive = false;
      }
    }
  }

  float error = line.error;
  if (!line.valid) {
    error = lastError;
  }

  float dError = error - lastError;
  lastError = error;
  float turn = s.kp * error + s.kd * dError;

  if (forceStraight) {
    LFR_HW_setMotor(baseSpeed, baseSpeed);
  } else {
    int left = baseSpeed + (int)turn;
    int right = baseSpeed - (int)turn;
    LFR_HW_setMotor(left, right);
  }

  if (runPhase == RunStep) {
    bool done = false;
    if (s.isChecksum) {
      done = checkChecksum(s, line.sum, now);
    } else {
      unsigned long dur = (s.value < 0) ? 0UL : (unsigned long)s.value;
      done = (now - stepStartMs >= dur);
    }

    if (done) {
      if (s.continueAfter && s.afterMs > 0) {
        runPhase = RunAfter;
        phaseStartMs = now;
      } else if (s.stopBetween && s.stopMs > 0) {
        runPhase = RunStop;
        phaseStartMs = now;
        LFR_HW_stop();
      } else {
        nextStep();
      }
    }
  } else if (runPhase == RunAfter) {
    if (now - phaseStartMs >= (unsigned long)s.afterMs) {
      if (s.stopBetween && s.stopMs > 0) {
        runPhase = RunStop;
        phaseStartMs = now;
        LFR_HW_stop();
      } else {
        nextStep();
      }
    }
  }
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
    // Commands from app: SEQ, SEQPD, SW1, RESET
    if (line.startsWith("SEQ=")) {
      parseSeq(line.substring(4));
    } else if (line.startsWith("SEQPD=")) {
      parseSeqPd(line.substring(6));
    } else if (line == "SW1=1") {
      Serial.println("[CMD] SW1");
      if (runPhase == RunIdle) {
        startRun();
      } else {
        resetRun();
      }
    } else if (line == "RESET=1") {
      Serial.println("[CMD] RESET");
      resetRun();
      LFR_sendLine("ACK:RESET");
    } else if (line == "CLEAR=1") {
      Serial.println("[CMD] CLEAR");
      resetRun();
      stepCount = 0;
      LFR_sendLine("ACK:CLEAR");
    }
  }

  updateRun();
}
