# Validation
Use MinGW g++ on PATH and Python 3 on Windows:
- python tests/run_protocol_tests.py
- python tests/watchdog_test.py
- python tests/joystick_test.py
- powershell -File tests/compile_examples.ps1 -ArduinoCli <path-to-arduino-cli.exe>

Protocol tests use public synthetic fixtures and independent Python SHA/HMAC,
never device credentials. They cover both advertised models, invalid configuration,
case normalization, handshake, authenticated packets, tampering, replay, Stop,
session replacement, fragmented/coalesced packets and expired fragments.

Watchdog tests extract and execute the production reset/watchdog/getter functions
with a deterministic clock, including 399/400 ms boundaries and clock wrap.
They do not simulate an entire BLE radio or certify actuator hardware safety.

Compile matrix: core esp32:esp32 3.3.11, generic ESP32 and ESP32-C3, all five examples.
Verified 2026-09-14: all 10 target/example builds passed. Protocol tests passed
60 assertions plus handshake checks; watchdog and joystick host tests passed.
Hardware validation of this Arduino port remains pending.

