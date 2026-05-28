// PBGamepad.cpp
#include "PBGamepad.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *PB_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLEServer         *pbServer      = nullptr;
static BLECharacteristic *pbTxChar      = nullptr;
static BLECharacteristic *pbRxChar      = nullptr;
static BLEAdvertising    *pbAdv         = nullptr;

#ifndef PB_BLE_STATUS_LED_PIN
#define PB_BLE_STATUS_LED_PIN 7
#endif
#ifndef PB_BLE_STATUS_LED_ACTIVE_HIGH
#define PB_BLE_STATUS_LED_ACTIVE_HIGH 1
#endif
#ifndef PB_BLE_STATUS_LED_BLINK_MS
#define PB_BLE_STATUS_LED_BLINK_MS 100
#endif

#if PB_BLE_STATUS_LED_PIN >= 0
static TaskHandle_t pb_statusLedTaskHandle = nullptr;
#endif

static volatile bool pb_deviceConnected = false;
static volatile bool pb_hasActiveConn = false;
static const uint16_t PB_INVALID_CONN_ID = 0xFFFF;
static volatile uint16_t pb_activeConnId = PB_INVALID_CONN_ID;
static uint32_t pb_rejectedConnCount = 0;
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
static const uint32_t PB_CONTROL_TIMEOUT_MS = 400;
static const uint32_t PB_ADV_RETRY_MS = 500;
static uint32_t pb_lastControlMs = 0;
static bool pb_advRetryPending = false;
static uint32_t pb_lastAdvAttemptMs = 0;
static uint32_t pb_lastDisconnectMs = 0;
static uint32_t pb_advRetryCount = 0;
static uint8_t pb_binBuf[10];
static uint8_t pb_binIdx = 0;

static void pb_processRxBuffer(void);
static bool pb_applyControlTimeout(void);
static void pb_tryRestartAdvertising(void);
static void pb_stopAdvertising(void);
static void pb_disconnectConn(BLEServer *pServer, uint16_t connId);
static void pb_handleConnect(BLEServer *pServer, uint16_t connId);
static void pb_handleDisconnect(uint16_t connId);
static const char *pb_resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

static void pb_logBootDiagnostics(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.print("[PB BLE] boot reset_reason=");
    Serial.print(pb_resetReasonName(reason));
    Serial.print("(");
    Serial.print((int)reason);
    Serial.print(") heap=");
    Serial.println(ESP.getFreeHeap());
}

#if PB_BLE_STATUS_LED_PIN >= 0
static void pb_statusLedWrite(bool on) {
#if PB_BLE_STATUS_LED_ACTIVE_HIGH
    digitalWrite(PB_BLE_STATUS_LED_PIN, on ? HIGH : LOW);
#else
    digitalWrite(PB_BLE_STATUS_LED_PIN, on ? LOW : HIGH);
#endif
}

static void pb_statusLedTask(void *param) {
    (void)param;
    pinMode(PB_BLE_STATUS_LED_PIN, OUTPUT);
    bool ledOn = false;
    pb_statusLedWrite(false);

    for (;;) {
        if (pb_deviceConnected) {
            pb_statusLedWrite(true);
            ledOn = true;
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            ledOn = !ledOn;
            pb_statusLedWrite(ledOn);
            vTaskDelay(pdMS_TO_TICKS((PB_BLE_STATUS_LED_BLINK_MS > 0) ? PB_BLE_STATUS_LED_BLINK_MS : 100));
        }
    }
}

static void pb_startStatusLedTask(void) {
    if (pb_statusLedTaskHandle != nullptr) {
        return;
    }

    BaseType_t ok = xTaskCreate(
        pb_statusLedTask,
        "pb_ble_led",
        2048,
        nullptr,
        1,
        &pb_statusLedTaskHandle
    );
    if (ok != pdPASS) {
        pb_statusLedTaskHandle = nullptr;
        Serial.println("[PB BLE] status_led_task_failed");
    }
}
#else
static void pb_startStatusLedTask(void) {}
#endif

static bool pb_startAdvertising(bool retry) {
    const uint32_t now = millis();
    pb_lastAdvAttemptMs = now;
    if (retry) {
        pb_advRetryCount++;
        Serial.print("[PB BLE] adv_retry count=");
        Serial.println(pb_advRetryCount);
    }

    if (pbAdv == nullptr) {
        Serial.println("[PB BLE] adv_failed reason=no_adv");
        return false;
    }

    const bool ok = pbAdv->start();
    if (ok) {
        pb_advRetryPending = false;
        Serial.print("[PB BLE] adv_start retry=");
        Serial.print(retry ? "true" : "false");
        Serial.print(" count=");
        Serial.println(pb_advRetryCount);
    } else {
        Serial.print("[PB BLE] adv_failed retry=");
        Serial.print(retry ? "true" : "false");
        Serial.print(" count=");
        Serial.println(pb_advRetryCount);
    }
    return ok;
}

void PBGamepad_resetControlState(void) {
    pb_hasBinary = false;
    pb_lx = 0;
    pb_ly = 0;
    pb_rx = 0;
    pb_ry = 0;
    pb_buttons = 0;
    pb_speed = -1;
    pb_lastLine = "0";
    pb_lastControlMs = 0;
    pb_binIdx = 0;
}

static void pb_markControlPacket(void) {
    pb_lastControlMs = millis();
}

static bool pb_applyControlTimeout(void) {
    if (pb_lastControlMs == 0) {
        return false;
    }
    if ((uint32_t)(millis() - pb_lastControlMs) > PB_CONTROL_TIMEOUT_MS) {
        PBGamepad_resetControlState();
        return false;
    }
    return true;
}

static void pb_stopAdvertising(void) {
    if (pbAdv == nullptr) {
        return;
    }
    pbAdv->stop();
}

static void pb_disconnectConn(BLEServer *pServer, uint16_t connId) {
    if (pServer == nullptr) {
        return;
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    pServer->disconnect(connId, BLE_ERR_REM_USER_CONN_TERM);
#elif defined(CONFIG_BLUEDROID_ENABLED)
    pServer->disconnect(connId);
#else
    (void)connId;
#endif
}

static void pb_tryRestartAdvertising(void) {
    if (pb_hasActiveConn || pb_deviceConnected || !pb_advRetryPending) {
        return;
    }

    const uint32_t now = millis();
    if (pb_lastAdvAttemptMs != 0 &&
        (uint32_t)(now - pb_lastAdvAttemptMs) < PB_ADV_RETRY_MS) {
        return;
    }

    pb_startAdvertising(true);
}

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
    pb_markControlPacket();

    return true;
}

static bool pb_isAsciiRxByte(uint8_t b) {
    return b == '\r' || b == '\n' || (b >= 32 && b <= 126);
}

static void pb_appendAsciiRxByte(uint8_t b) {
    if (!pb_isAsciiRxByte(b)) {
        return;
    }
    pb_rxBuffer += (char)b;
    if (pb_rxBuffer.length() > 160) {
        pb_rxBuffer.remove(0, pb_rxBuffer.length() - 160);
    }
}

static void pb_feedRxByte(uint8_t b) {
    if (pb_binIdx == 0) {
        if (b == 0xAA) {
            pb_binBuf[pb_binIdx++] = b;
            return;
        }
        pb_appendAsciiRxByte(b);
        return;
    }

    if (pb_binIdx == 1) {
        if (b == 0x55) {
            pb_binBuf[pb_binIdx++] = b;
            return;
        }
        pb_binIdx = 0;
        if (b == 0xAA) {
            pb_binBuf[pb_binIdx++] = b;
            return;
        }
        pb_appendAsciiRxByte(b);
        return;
    }

    pb_binBuf[pb_binIdx++] = b;
    if (pb_binIdx >= sizeof(pb_binBuf)) {
        pb_tryParseBinary(pb_binBuf, sizeof(pb_binBuf));
        pb_binIdx = 0;
    }
}

static void pb_handleConnect(BLEServer *pServer, uint16_t connId) {
    if (pb_hasActiveConn && pb_activeConnId != connId) {
        pb_rejectedConnCount++;
        Serial.print("[PB BLE] duplicate_rejected conn_id=");
        Serial.print(connId);
        Serial.print(" active_conn_id=");
        Serial.print(pb_activeConnId);
        Serial.print(" count=");
        Serial.println(pb_rejectedConnCount);
        pb_disconnectConn(pServer, connId);
        return;
    }

    pb_hasActiveConn = true;
    pb_activeConnId = connId;
    pb_deviceConnected = true;
    pb_advRetryPending = false;
    pb_stopAdvertising();
    Serial.print("[PB BLE] connected conn_id=");
    Serial.println(connId);
}

static void pb_handleDisconnect(uint16_t connId) {
    if (!pb_hasActiveConn || pb_activeConnId != connId) {
        Serial.print("[PB BLE] disconnect_ignored conn_id=");
        Serial.print(connId);
        Serial.print(" active_conn_id=");
        Serial.println(pb_activeConnId);
        return;
    }

    pb_hasActiveConn = false;
    pb_activeConnId = PB_INVALID_CONN_ID;
    pb_deviceConnected = false;
    pb_lastDisconnectMs = millis();
    pb_advRetryPending = true;
    pb_lastAdvAttemptMs = 0;
    PBGamepad_resetControlState();
    Serial.print("[PB BLE] disconnected conn_id=");
    Serial.println(connId);
}

class PB_ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {}
    void onDisconnect(BLEServer *pServer) override {}

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        pb_handleConnect(pServer, param->connect.conn_id);
    }

    void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        pb_handleDisconnect(param->disconnect.conn_id);
    }
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        pb_handleConnect(pServer, desc->conn_handle);
    }

    void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        pb_handleDisconnect(desc->conn_handle);
    }
#endif
};

class PB_RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        const uint8_t *data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();
        if (len == 0) return;

        bool textDigitsOnly = true;
        for (size_t i = 0; i < len; i++) {
            uint8_t b = data[i];
            if (!(b == '\r' || b == '\n' || b == ' ' || b == '\t' || (b >= '0' && b <= '9'))) {
                textDigitsOnly = false;
                break;
            }
        }
        if (textDigitsOnly) {
            String v = pCharacteristic->getValue();
            String trimmed = v;
            trimmed.trim();
            if (trimmed.length() > 0) {
                if (trimmed == "0") {
                    PBGamepad_resetControlState();
                    return;
                }
                pb_buttons = (uint16_t)trimmed.toInt();
                pb_lastLine = trimmed;
                pb_hasBinary = false;
                pb_markControlPacket();
                return;
            }
        }

        for (size_t i = 0; i < len; ++i) {
            pb_feedRxByte(data[i]);
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
                    pb_hasBinary = false;
                    pb_markControlPacket();
                    Serial.print("[PB BLE RX] ");
                    Serial.println(pb_lastLine);
                } else if (line == "0") {
                    PBGamepad_resetControlState();
                    Serial.print("[PB BLE RX] ");
                    Serial.println(pb_lastLine);
                } else {
                    pb_lastLine = line;
                    pb_hasBinary = false;
                    pb_markControlPacket();
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
    pb_logBootDiagnostics();
    pb_hasActiveConn = false;
    pb_activeConnId = PB_INVALID_CONN_ID;
    pb_deviceConnected = false;

    BLEDevice::init(deviceName);

    pbServer = BLEDevice::createServer();
    pbServer->advertiseOnDisconnect(false);
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
    pb_startAdvertising(false);
    pb_startStatusLedTask();
}

String PBGamepad_getCommand(void) {
    PBGamepad_poll();
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
    PBGamepad_poll();
    return pb_deviceConnected;
}

bool PBGamepad_isControlFresh(void) {
    PBGamepad_poll();
    return pb_applyControlTimeout();
}

uint32_t PBGamepad_lastControlAgeMs(void) {
    PBGamepad_poll();
    if (pb_lastControlMs == 0) {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(millis() - pb_lastControlMs);
}

void PBGamepad_poll(void) {
    pb_processRxBuffer();
    pb_applyControlTimeout();
    pb_tryRestartAdvertising();
}

uint32_t PBGamepad_lastDisconnectAgeMs(void) {
    PBGamepad_poll();
    if (pb_lastDisconnectMs == 0) {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(millis() - pb_lastDisconnectMs);
}

uint32_t PBGamepad_advertiseRetryCount(void) {
    PBGamepad_poll();
    return pb_advRetryCount;
}

bool PBGamepad_hasBinary(void) {
    PBGamepad_poll();
    return pb_hasBinary && pb_applyControlTimeout();
}

float PBGamepad_getLX(void) {
    PBGamepad_poll();
    return (float)pb_lx / 100.0f;
}

float PBGamepad_getLY(void) {
    PBGamepad_poll();
    return (float)pb_ly / 100.0f;
}

float PBGamepad_getRX(void) {
    PBGamepad_poll();
    return (float)pb_rx / 100.0f;
}

float PBGamepad_getRY(void) {
    PBGamepad_poll();
    return (float)pb_ry / 100.0f;
}

uint16_t PBGamepad_getButtons(void) {
    PBGamepad_poll();
    return pb_buttons;
}

int PBGamepad_getSpeed(void) {
    PBGamepad_poll();
    return pb_speed;
}

#else
void PBGamepad_init(const char *) {}
String PBGamepad_getCommand(void) { return String(""); }
void PBGamepad_sendLine(const String &) {}
bool PBGamepad_isConnected(void) { return false; }
bool PBGamepad_isControlFresh(void) { return false; }
void PBGamepad_resetControlState(void) {}
uint32_t PBGamepad_lastControlAgeMs(void) { return 0xFFFFFFFFUL; }
void PBGamepad_poll(void) {}
uint32_t PBGamepad_lastDisconnectAgeMs(void) { return 0xFFFFFFFFUL; }
uint32_t PBGamepad_advertiseRetryCount(void) { return 0; }
bool PBGamepad_hasBinary(void) { return false; }
float PBGamepad_getLX(void) { return 0.0f; }
float PBGamepad_getLY(void) { return 0.0f; }
float PBGamepad_getRX(void) { return 0.0f; }
float PBGamepad_getRY(void) { return 0.0f; }
uint16_t PBGamepad_getButtons(void) { return 0; }
int PBGamepad_getSpeed(void) { return -1; }
#endif
