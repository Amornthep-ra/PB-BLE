"""Run production watchdog/reset/getter functions with a deterministic clock.
No BLE mock decides the timeout outcome: production code performs it.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + " {")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    source = (ROOT / "src/esp32/PBGamepad.cpp").read_text(encoding="utf-8")
    harness = r'''
#include <atomic>
#include <cstdint>
#include <cassert>
struct PBGamepadAxes { float lx,ly,rx,ry; };
struct SerialMock { template<class T> void print(T) {} template<class T> void println(T) {} } Serial;
static uint32_t nowMs=1000, pb_lastControlMs=1000;
uint32_t millis(){return nowMs;}
static int pb_stateMux=0;
void portENTER_CRITICAL(int*){}
void portEXIT_CRITICAL(int*){}
static bool pb_hasActiveConn=true, pb_controlFresh=true, pb_hasBinary=true;
static std::atomic<bool> pb_authenticated(true);
static std::atomic<uint8_t> pb_lastLineState(0);
static int8_t pb_lx=50,pb_ly=-60,pb_rx=70,pb_ry=-80;
static uint16_t pb_buttons=1;
static int pb_speed=2, stops=0;
static const uint32_t PB_CONTROL_TIMEOUT_MS=400;
static void pb_invokeEmergencyStop(){
  assert(!pb_controlFresh && pb_lx==0 && pb_buttons==0); ++stops;
}
'''
    for signature in (
        "void PBGamepad_resetControlState(void)",
        "uint32_t PBGamepad_lastControlAgeMs(void)",
        "bool PBGamepad_readAxes(PBGamepadAxes *axes)",
        "uint16_t PBGamepad_getButtons(void)",
        "int PBGamepad_getSpeed(void)",
        "static void pb_serviceControlWatchdog(void)",
    ):
        harness += function(source, signature)
    harness += r'''
int main(){
  PBGamepadAxes a={};
  nowMs=1399; pb_serviceControlWatchdog();
  assert(stops==0 && PBGamepad_readAxes(&a) && a.lx==0.5f);
  nowMs=1400;
  // Getters reject stale data even before the worker gets CPU time.
  assert(!PBGamepad_readAxes(&a) && a.lx==0 && a.ry==0);
  assert(PBGamepad_getButtons()==0 && PBGamepad_getSpeed()==-1);
  pb_serviceControlWatchdog();
  assert(stops==1 && pb_hasActiveConn && pb_authenticated.load());
  nowMs=5000; pb_serviceControlWatchdog(); assert(stops==1);
  // Simulate publishing a new already-authenticated packet (protocol acceptance
  // and replay rejection are exercised independently by run_protocol_tests).
  pb_lastControlMs=nowMs; pb_controlFresh=pb_hasBinary=true; pb_lx=25;
  assert(PBGamepad_readAxes(&a) && a.lx==0.25f);
  pb_serviceControlWatchdog(); assert(stops==1);
  pb_lastControlMs=uint32_t(20-399); nowMs=20;
  pb_serviceControlWatchdog(); assert(stops==1);
  nowMs=21; pb_serviceControlWatchdog(); assert(stops==2);
}
'''
    with tempfile.TemporaryDirectory(prefix="kb-watchdog-") as directory:
        exe = pathlib.Path(directory) / "watchdog.exe"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-x", "c++", "-", "-o", str(exe)],
                       input=harness, text=True, check=True)
        subprocess.run([str(exe)], check=True)
    print("PASS: production watchdog stops at 400ms, retains link/auth, resumes, handles wrap; stale getters zero")


if __name__ == "__main__":
    main()


