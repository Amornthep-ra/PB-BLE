"""Exercise actual platform-neutral joystick helpers without GPIO or a BLE radio."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r"""
#include <cassert>
#include <cmath>
#include "PBJoystick.h"
static bool fresh = true;
static PBGamepadAxes current = {0.2f, -0.6f, 0.8f, 1.0f};
bool PBGamepad_isControlFresh() { return fresh; }
void PBGamepad_poll() {}
bool PBGamepad_readAxes(PBGamepadAxes* out) {
  *out = fresh ? current : PBGamepadAxes{0,0,0,0}; return fresh;
}
int main() {
  PB_JoystickDual_updateAxes();
  assert(PB_JoystickDual_getLX100() == 20);
  assert(PB_JoystickDual_getLY100() == -60);
  assert(PB_JoystickDual_getRX100() == 80);
  assert(PB_JoystickDual_getRY100() == 100);
  fresh = false;
  assert(PB_JoystickDual_getLY100() == 0);
  PB_JoystickDual_updateAxes();
  assert(PB_JoystickDual_getRX100() == 0);
  assert(PB_BLE_ApplyDeadzone(.05f,.1f) == 0);
  assert(PB_BLE_ArcadeMixLeft(1,0) == 100);
  assert(PB_BLE_ArcadeMixRight(1,0) == 100);
  assert(PB_BLE_ArcadeMixLeft(1,1) == 100);
  assert(PB_BLE_ArcadeMixRight(1,1) == 0);
  assert(PB_BLE_LimitSpeed(0,20,80) == 0);
  assert(PB_BLE_LimitSpeed(-5,20,80) == -20);
  assert(PB_BLE_LimitSpeed(100,20,80) == 80);
}
"""
with tempfile.TemporaryDirectory(prefix="pb-joystick-") as directory:
    temp = pathlib.Path(directory)
    (temp / "Arduino.h").write_text(
        "#pragma once\n#include <cstdint>\n#include <cstdlib>\nclass String;\n",
        encoding="utf-8")
    (temp / "sdkconfig.h").write_text(
        "#define CONFIG_IDF_TARGET_ESP32 1\n", encoding="utf-8")
    exe = temp / "joystick.exe"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DARDUINO_ARCH_ESP32", "-I"+str(temp), "-I"+str(ROOT/"src"),
                    "-x", "c++", "-", str(ROOT/"src/PBJoystick.cpp"),
                    "-o", str(exe)], input=SOURCE, text=True, check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: joystick snapshot, stale values, directions and pure math helpers")

