// PBGamepad.cpp
#include "PBGamepad.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char *PB_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLEServer         *pbServer      = nullptr;
static BLECharacteristic *pbTxChar      = nullptr;
static BLECharacteristic *pbRxChar      = nullptr;
static BLEAdvertising    *pbAdv         = nullptr;

static bool   pb_deviceConnected = false;
static String pb_rxBuffer;
static String pb_buf;
static String pb_lastLine;
static bool   pb_hasBinary = false;
static int8_t pb_lx = 0;
static int8_t pb_ly = 0;
static int8_t pb_rx = 0;
static int8_t pb_ry = 0;
static uint16_t pb_buttons = 0;
static int pb_speed = -1;

static bool pb_tryParseBinary(const uint8_t *data, size_t len) {
    if (len != 10) return false;
    if (data[0] != 0xAA || data[1] != 0x55) return false;

    uint8_t cs = 0;
    for (int i = 2; i <= 8; i++) {
        cs = (uint8_t)((cs + data[i]) & 0xFF);
    }
    if (cs != data[9]) return false;

    pb_lx = (int8_t)data[3];
    pb_ly = (int8_t)data[4];
    pb_rx = (int8_t)data[5];
    pb_ry = (int8_t)data[6];

    pb_buttons = (uint16_t)data[7] | ((uint16_t)data[8] << 8);
    pb_lastLine = "";
    pb_hasBinary = true;

    int newSpeed = -1;
    if (pb_buttons & PB_BLE_BTN_SPEED_HIGH) {
        newSpeed = 2;
    } else if (pb_buttons & PB_BLE_BTN_SPEED_MID) {
        newSpeed = 1;
    } else if (pb_buttons & PB_BLE_BTN_SPEED_LOW) {
        newSpeed = 0;
    }
    if (newSpeed != -1) {
        pb_speed = newSpeed;
    }

    return true;
}

class PB_ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {
        pb_deviceConnected = true;
        Serial.println("[PB BLE] Client connected");
    }

    void onDisconnect(BLEServer *pServer) override {
        pb_deviceConnected = false;
        Serial.println("[PB BLE] Client disconnected, restart advertising");
        pb_hasBinary = false;
        pb_lx = 0;
        pb_ly = 0;
        pb_rx = 0;
        pb_ry = 0;
        pb_buttons = 0;
        pb_speed = -1;
        pb_lastLine = "0";
        if (pbAdv) {
            pbAdv->start();
        }
    }
};

class PB_RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        const uint8_t *data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();
        if (len == 0) return;

        if (pb_tryParseBinary(data, len)) {
            return;
        }

        String v = pCharacteristic->getValue();
        if (v.length() == 0) return;

        for (size_t i = 0; i < v.length(); ++i) {
            pb_rxBuffer += v[i];
        }
    }
};

static void pb_processRxBuffer() {
    while (pb_rxBuffer.length() > 0) {
        char ch = pb_rxBuffer[0];
        pb_rxBuffer.remove(0, 1);

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            String line = pb_buf;
            pb_buf = "";
            line.trim();

            if (line.length() > 0) {
                if (line == "PING") {
                    PBGamepad_sendLine("PONG");
                    Serial.println("[PB BLE SYS] PING -> PONG");
                } else if (line == "HELLO_APP" || line.startsWith("ACK:")) {
                    Serial.print("[PB BLE SYS] ");
                    Serial.println(line);
                } else if (line == "Low" || line == "Medium" || line == "High") {
                    if (line == "Low") pb_speed = 0;
                    if (line == "Medium") pb_speed = 1;
                    if (line == "High") pb_speed = 2;
                    pb_lastLine = line;
                    Serial.print("[PB BLE RX] ");
                    Serial.println(pb_lastLine);
                } else {
                    pb_lastLine = line;
                    Serial.print("[PB BLE RX] ");
                    Serial.println(pb_lastLine);
                }
            }
        } else {
            pb_buf += ch;
            if (pb_buf.length() > 128) {
                pb_buf.remove(0, pb_buf.length() - 128);
            }
        }
    }
}

void PBGamepad_init(const char *deviceName) {
    Serial.println("[PB BLE] init start");

    BLEDevice::init(deviceName);

    pbServer = BLEDevice::createServer();
    pbServer->setCallbacks(new PB_ServerCallbacks());

    BLEService *svc = pbServer->createService(PB_SERVICE_UUID);

    pbTxChar = svc->createCharacteristic(
        PB_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
#if defined(CONFIG_BLUEDROID_ENABLED)
    pbTxChar->addDescriptor(new BLE2902());
#endif

    pbRxChar = svc->createCharacteristic(
        PB_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pbRxChar->setCallbacks(new PB_RxCallbacks());

    svc->start();

    pbAdv = BLEDevice::getAdvertising();
    pbAdv->addServiceUUID(PB_SERVICE_UUID);
    pbAdv->setScanResponse(true);
    pbAdv->setMinPreferred(0x06);
    pbAdv->setMinPreferred(0x12);
    pbAdv->start();

    Serial.println("[PB BLE] Advertising started");
}

String PBGamepad_getCommand(void) {
    pb_processRxBuffer();
    return pb_lastLine;
}

void PBGamepad_sendLine(const String &msg) {
    if (!pb_deviceConnected || pbTxChar == nullptr) return;

    String out = msg;
    out += '\n';
    pbTxChar->setValue(out.c_str());
    pbTxChar->notify();

    Serial.print("[PB BLE TX] ");
    Serial.println(msg);
}

bool PBGamepad_isConnected(void) {
    return pb_deviceConnected;
}

bool PBGamepad_hasBinary(void) {
    return pb_hasBinary;
}

float PBGamepad_getLX(void) {
    return (float)pb_lx / 100.0f;
}

float PBGamepad_getLY(void) {
    return (float)pb_ly / 100.0f;
}

float PBGamepad_getRX(void) {
    return (float)pb_rx / 100.0f;
}

float PBGamepad_getRY(void) {
    return (float)pb_ry / 100.0f;
}

uint16_t PBGamepad_getButtons(void) {
    return pb_buttons;
}

int PBGamepad_getSpeed(void) {
    return pb_speed;
}

#else
void PBGamepad_init(const char *) {}
String PBGamepad_getCommand(void) { return String(""); }
void PBGamepad_sendLine(const String &) {}
bool PBGamepad_isConnected(void) { return false; }
bool PBGamepad_hasBinary(void) { return false; }
float PBGamepad_getLX(void) { return 0.0f; }
float PBGamepad_getLY(void) { return 0.0f; }
float PBGamepad_getRX(void) { return 0.0f; }
float PBGamepad_getRY(void) { return 0.0f; }
uint16_t PBGamepad_getButtons(void) { return 0; }
int PBGamepad_getSpeed(void) { return -1; }
#endif


