# PB-BLE for PB Controller

## วิธีติดตั้งสำหรับผู้เริ่มต้น

ไลบรารีนี้ใช้รับข้อมูลปุ่มและจอยจากแอป PB Controller ไม่ได้สั่งมอเตอร์ให้เอง
รองรับ ESP32 และ ESP32-C3 ในรอบนี้ บอร์ดที่มี BLE รุ่นอื่นยังไม่รองรับ

1. ไปที่ [หน้าดาวน์โหลดล่าสุด](https://github.com/Amornthep-ra/PB-BLE/releases/latest)
   แล้วดาวน์โหลด **PB-BLE.zip** ในหัวข้อ Assets ไม่ต้องแตก ZIP
2. เปิด Arduino IDE ไปที่ **Sketch > Include Library > Add .ZIP Library…**
   แล้วเลือกไฟล์ PB-BLE.zip รอข้อความติดตั้งสำเร็จ
3. เปิด **Tools > Board > Boards Manager…** ค้นหา **esp32**
   แล้วติดตั้ง **esp32 by Espressif Systems** เวอร์ชัน **3.3.11** ที่ใช้ทดสอบ compile
4. เลือกบอร์ดจริงที่ใช้งานใน **Tools > Board** และเลือก **Tools > Port**
   ตัวอย่างบอร์ดทั่วไปคือ ESP32 Dev Module หรือ ESP32C3 Dev Module
5. เปิด **File > Examples > PB-BLE > Connection_Status**
6. เปลี่ยนค่าของตัวเองในคำสั่งนี้:

   ```cpp
   PBGamepad_init("MyRobot", "Robot01", "YourCode123");
   ```

   - ตัวแรก: ชื่ออุปกรณ์ BLE ที่แสดงในแอป
   - ตัวที่สอง: Board ID ใช้อังกฤษ/ตัวเลข 1–32 ตัว และไม่ควรซ้ำกับบอร์ดอื่น
   - ตัวที่สาม: Pair Code ใช้อังกฤษ/ตัวเลข 8–16 ตัว ไม่แยกตัวเล็ก–ใหญ่
   - เปลี่ยนรหัสตัวอย่างก่อนใช้งานจริง ห้ามใส่ช่องว่างหรือสัญลักษณ์ใน ID/Code
7. กด **Upload** เปิด Serial Monitor ที่ **115200 baud**
8. เปิดแอป PB Controller เลือกเชื่อม BLE ชื่อที่ตั้งไว้ กรอก Pair Code เมื่อแอปถาม
   แล้วเข้าโหมดควบคุมและกด Start ก่อนทดลองปุ่มหรือจอย

ตัวอย่าง Connection_Status แสดงสถานะเท่านั้น หากต้องการอ่านคำสั่งให้ลอง
Gamepad_4_Button, Gamepad_8_Button หรือ Joystick_Dual ส่วน Safe_Stop
แสดงวิธีตรวจข้อมูลหมดอายุและรับเหตุการณ์หยุด ผู้ใช้ต้องเขียนคำสั่งหยุดอุปกรณ์เอง
ทดสอบโดยยกล้อหรือถอดกำลังมอเตอร์ก่อน และอย่าคาดว่าตัวอย่าง Serial จะขับหุ่นยนต์ได้ทันที

### ถ้าติดตั้งหรือใช้งานไม่ได้

- ไม่พบ PB-BLE ใน Examples: ปิดเปิด IDE ใหม่ และตรวจว่าติดตั้ง ZIP สำเร็จ
- ZIP ติดตั้งไม่ได้: ใช้ไฟล์ **PB-BLE.zip** ใน Assets ของ Release ตามขั้นตอนด้านบน
- หา esp32 ใน Boards Manager ไม่พบ: เพิ่ม URL นี้ใน
  **File > Preferences > Additional Boards Manager URLs** แล้วเปิด Boards Manager ใหม่:
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
- แจ้งบอร์ดไม่รองรับ: เลือกบอร์ด ESP32 หรือ ESP32-C3 ที่ถูกต้อง ไม่ใช่ UNO หรือ ESP32-S2/S3
- sketch เก่า compile ไม่ผ่าน: เอา PBGamepad.h/.cpp และ PBJoystick.h/.cpp
  ที่เคยคัดลอกไว้ข้างไฟล์ .ino ออกก่อน เพื่อไม่ให้บัง Lib ที่ติดตั้งใหม่
  จากนั้นเริ่มจากตัวอย่างใหม่ ใช้ init สามค่า ไม่ใช่ชื่ออุปกรณ์ค่าเดียวแบบเก่า
- เชื่อมไม่ได้: ตรวจไฟเลี้ยง, Bluetooth/สิทธิ์ของแอป และไม่ให้มือถืออีกเครื่องเชื่อมบอร์ดค้างอยู่
- รหัสไม่ผ่าน: ตรวจรหัสใน sketch ที่ Upload จริง และใช้ Board ID แยกแต่ละอุปกรณ์

**สถานะการทดสอบ:** compile ตัวอย่างครบ 5 แบบบน ESP32 และ ESP32-C3
ด้วย core 3.3.11 ผ่าน รวม 10 ชุด และ host tests ผ่าน
การทดสอบ Arduino port นี้กับบอร์ดจริงยังรอการยืนยัน

---

Arduino communication library for the PB Controller app (PB2 protocol).
This library reads authenticated buttons and axes. It does not drive motors,
choose GPIO pins, initialize robot hardware, or require a motor-driver library.

## Supported targets
- Arduino ESP32 core 3.3.11: ESP32 and ESP32-C3.
- Other targets fail compilation explicitly. Having BLE alone is not sufficient.
- Compilation and host tests are not physical-board validation. Hardware checks
  with this Arduino port are pending; the KBIDE implementation is the reference.

## Installation
Install Espressif ESP32 core 3.3.11 in Arduino IDE Boards Manager.
Copy this PB-BLE folder (the one containing library.properties) into your
sketchbook libraries directory and restart Arduino IDE, or install a ZIP whose
library root contains library.properties and src/.
Do NOT install the entire combined repository ZIP as a single Arduino library.
Select your actual ESP32 or ESP32-C3 board in Tools > Board.
Use File > Examples > PB-BLE. Use BLE bundled with the ESP32 core; do not copy
KBIDE BLE headers or install an old standalone ESP32 BLE Arduino library.

## Setup
Include <PBGamepad.h>, then call:
PBGamepad_init("PB-Robot", "Robot01", "ChangeMe123");
Example values are public placeholders, not secure credentials; replace them.
Device Name must be nonempty. Board ID is 1-32 ASCII English letters/digits and
must be unique among your devices. Pair Code is 8-16 ASCII English letters/digits,
case insensitive. Spaces, hyphens, punctuation, and non-ASCII text are rejected.
Call init once from setup. Register your stop callback before init, and put
actuators in a safe stopped state before BLE startup. BLE shares chip resources
with your other libraries: validate your motor/PWM setup on the actual board.

## Reading data
Call PBGamepad_poll() regularly in loop. On supported ESP32 targets a dedicated
worker services BLE and the watchdog; poll also provides fallback servicing.
Do not block loop when controlling physical hardware.

- PBGamepad_isConnected(): a BLE link exists, NOT permission to drive.
- PBGamepad_isAuthenticated(): current PB2 session passed authentication.
- PBGamepad_isControlFresh(): authenticated control data younger than 400 ms.
- PBGamepad_readAxes(&axes): one coherent LX/LY/RX/RY snapshot, floats -1..1.
  Returns false and zeros on stale/invalid control data.
- PBGamepad_getButtons(): 16-bit button/speed mask.
- PB_GetButtonsLow(): CMD/data byte (0..255), not a button ordinal.
  Up=1, Down=2, Left=4, Right=8; Triangle=16, Cross=32, Square=64, Circle=128.
  Simultaneous presses combine bits. Equality tests a whole combination;
  bitwise AND tests whether a bit is included.
- PB_GetSpeedLevel(): Lo=1, Med=2, Hi=4 (bit values).
- PBGamepad_getSpeed(): legacy speed index Lo=0, Med=1, Hi=2; unavailable=-1.
- PB_GetDriveSpeed()/PB_GetTurnSpeed(): 0..100 in 4BTN/8BTN.
  These fields share LX/RX on the wire; do not interpret them as drive/turn speed
  while using Joystick mode.
- PB_SpeedFromLevel(): fixed convenience mapping 25/50/100, NOT a reading of
  the app's configured DRV/TRN. Prefer PB_GetDriveSpeed/PB_GetTurnSpeed.
- PBGamepad_getCommand(): legacy text accessor only; PB2 does not deliver the old
  string commands. Read PB_GetButtonsLow() instead.
- PBGamepad_sendLine(): raw newline-terminated BLE notifications, not a promise
  that the app displays arbitrary text. Reserved for protocol/internal use;
  do not call concurrently with pairing or from the stop callback.

Include <PBJoystick.h> for PB_JoystickDual_updateAxes() and
PB_JoystickDual_getLX100/LY100/RX100/RY100(), returning -100..100.
Call updateAxes before reading that group. Raw axes are not inverted:
up Y is negative; down Y is positive; left X is negative; right X is positive.
Choose hardware direction yourself.
Math helpers are calculations only: ApplyDeadzone uses normalized values;
ArcadeMixLeft/Right accepts forward/turn normalized to -1..1 and returns signed
-100..100, normalized as a pair; LimitSpeed preserves sign and zero.

## Safety contract
Stop, BLE disconnection, handshake invalidation, or a 400 ms control timeout
clears the control data. Timeout retains the authenticated connection and
sequence checks; only a newer valid packet can resume data.
The worker checks roughly every 5 ms; 400 ms is a freshness threshold, not a
hard real-time hardware stop guarantee under CPU starvation.
Register PBGamepad_setEmergencyStopCallback(callback). Normally it runs in the
BLE worker task, not an ISR; fallback poll servicing can run it in loop.
It may run during pairing even before control starts. Keep it short, nonblocking,
and thread-safe. Do not call BLE, delay, Serial, or UI code from it.
Use a thread-safe hardware stop or signal loop with an atomic flag. A flag cannot
stop a motor while your loop is blocked. Serialize actuator writes so an older
loop iteration cannot overwrite a stop.
Always implement your own actuator stop in the !isControlFresh path.
Zero values inside a communication library do not physically stop a motor.

## Migration from the old examples_arduino
Remove sketch-local PBGamepad.cpp/.h and PBJoystick.cpp/.h; otherwise they shadow
the installed library. The old duplicate files are replaced by examples/ here.
Change the one-argument init to the three-argument init above.
Use angle-bracket library includes. Add regular poll/update and fresh-data checks.
Puppybot Serial1 helpers, motor mapping and motor-writing joystick functions are
not part of this library. Implement those in your sketch or a separate adapter.
Line Follower is not included in this release.

## Architecture and extending targets
src/internal/PB2Protocol.h is platform-independent PB2 framing/authentication
with injected SHA-256, HMAC-SHA256 and secure random callbacks.
src/esp32/PBGamepad.cpp owns BLE transport, task/queue, clock and shared state.
Public headers expose the same data API regardless of future backend.
A new backend must supply secure cryptography/randomness, connection identity,
bounded receive handling, consistent state snapshots and the watchdog contract.
Never silently accept an unsupported core. Extend model validation and the
compile guard only with protocol tests and a real backend, not by relabeling a chip.
PB2 framing, UUIDs, signature material and sequence semantics match KBIDE.
No plaintext pairing credential is logged; temporary key material is erased by
the protocol core. The board credential still exists in your compiled sketch.

## Validation
Windows: python tests/run_protocol_tests.py (MinGW g++ required).
Windows: python tests/watchdog_test.py.
Windows: python tests/joystick_test.py (axis direction, stale values, pure math).
The former uses Windows BCrypt and an independent Python SHA/HMAC reference;
the latter executes production watchdog/getters with a deterministic clock.
Compile examples for esp32:esp32:esp32 and esp32:esp32:esp32c3.
Before deployment test first pairing, saved-code reuse, changed code, reconnect,
4BTN/8BTN/Joystick, releases, Stop, stale data and your actual actuator stop.
Do not claim untested boards as hardware-validated.

