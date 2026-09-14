// PBGamepad.cpp
#include "../PBGamepad.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "../internal/PB2Protocol.h"

static bool pb_sha(const uint8_t *p, size_t n, uint8_t *out) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256(p, n, out, 0) == 0;
#else
    return mbedtls_sha256_ret(p, n, out, 0) == 0;
#endif
}
static bool pb_hmac(const uint8_t *key, const uint8_t *p, size_t n, uint8_t *out) {
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md && mbedtls_md_hmac(md, key, 32, p, n, out) == 0;
}
static bool pb_random(uint8_t *p, size_t n) {
    esp_fill_random(p, n);
    return true;
}
static PB2Protocol pb_protocol(pb_sha, pb_hmac, pb_random);
// BLE callbacks only enqueue. A dedicated worker owns protocol/session state.
struct PB_Event {
    uint8_t type; // 1 connect, 2 disconnect, 3 data
    uint16_t conn;
    uint16_t length;
    uint32_t received;
    uint8_t bytes[512];
    uint16_t reason;
};
static QueueHandle_t pb_events = nullptr;
static TaskHandle_t pb_controlWorkerHandle = nullptr;
static std::atomic<bool> pb_queueFailed(false);
static std::atomic<bool> pb_authenticated(false);
static std::atomic<PBGamepadStopCallback> pb_emergencyStopCallback(nullptr);
static portMUX_TYPE pb_stateMux = portMUX_INITIALIZER_UNLOCKED;
static void pb_enqueue(uint8_t type, uint16_t conn, const uint8_t *p = nullptr, size_t n = 0, uint16_t reason = 0xFFFF) {
    if (!pb_events || n > 512) { pb_queueFailed.store(true); return; }
    PB_Event event = {};
    event.type = type; event.conn = conn; event.length = n;
    event.reason = reason;
    event.received = millis();
    if (n) memcpy(event.bytes, p, n);
    if (xQueueSend(pb_events, &event, 0) != pdTRUE) pb_queueFailed.store(true);
}

static const char *PB_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *PB_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLEServer         *pbServer      = nullptr;
static BLECharacteristic *pbTxChar      = nullptr;
static BLECharacteristic *pbRxChar      = nullptr;
static BLEAdvertising    *pbAdv         = nullptr;

static std::atomic<bool> pb_deviceConnected(false);
static volatile bool pb_hasActiveConn = false;
static const uint16_t PB_INVALID_CONN_ID = 0xFFFF;
static volatile uint16_t pb_activeConnId = PB_INVALID_CONN_ID;
static uint32_t pb_rejectedConnCount = 0;
static PB2Stream pb_stream;
static bool pb_controlFresh = false;
static uint32_t pb_packetReceived = 0;
// Legacy command state is only empty or "0" in PB2. Keep it atomic so the
// Arduino loop never races a heap-allocating String against the worker task.
static std::atomic<uint8_t> pb_lastLineState(1);
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


static void pb_processEvents(void);
static void pb_clearSession(void);
static bool pb_applyControlTimeout(void);
static void pb_controlWorkerTask(void *param);
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
    portENTER_CRITICAL(&pb_stateMux);
    pb_hasBinary = false;
    pb_lx = 0;
    pb_ly = 0;
    pb_rx = 0;
    pb_ry = 0;
    pb_buttons = 0;
    pb_speed = -1;
    pb_lastControlMs = 0;
    pb_controlFresh = false;
    portEXIT_CRITICAL(&pb_stateMux);
    pb_lastLineState.store(1);
}

void PBGamepad_setEmergencyStopCallback(PBGamepadStopCallback callback) {
    pb_emergencyStopCallback.store(callback);
}

static void pb_invokeEmergencyStop(void) {
    PBGamepadStopCallback callback = pb_emergencyStopCallback.load();
    if (callback) callback();
}

static bool pb_applyControlTimeout(void) {
    bool fresh;
    uint32_t last;
    portENTER_CRITICAL(&pb_stateMux);
    fresh = pb_controlFresh;
    last = pb_lastControlMs;
    portEXIT_CRITICAL(&pb_stateMux);
    if (!fresh || !pb_authenticated.load()) {
        return false;
    }
    if ((uint32_t)(millis() - last) >= PB_CONTROL_TIMEOUT_MS) {
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

static void pb_clearSession(void) {
    pb_protocol.reset();
    pb_authenticated.store(false);
    PBGamepad_resetControlState();
    pb_stream.reset();
}

static void pb_tryParseBinary(const uint8_t *data, size_t len) {
    if ((uint32_t)(millis() - pb_packetReceived) >= PB_CONTROL_TIMEOUT_MS) return;
    uint8_t payload[6] = {};
    int type = pb_protocol.packet(data, len, payload);
    if (type == 2) {
        PBGamepad_resetControlState();
        pb_invokeEmergencyStop();
        return;
    }
    if (type != 1) return;
    portENTER_CRITICAL(&pb_stateMux);
    pb_lx = int8_t(payload[0]); pb_ly = int8_t(payload[1]);
    pb_rx = int8_t(payload[2]); pb_ry = int8_t(payload[3]);
    pb_buttons = uint16_t(payload[4]) | (uint16_t(payload[5]) << 8);
    const int newSpeed = (pb_buttons & PB_BLE_BTN_SPEED_HIGH) ? 2 :
               (pb_buttons & PB_BLE_BTN_SPEED_MID) ? 1 :
               (pb_buttons & PB_BLE_BTN_SPEED_LOW) ? 0 : -1;
    if (newSpeed != -1) pb_speed = newSpeed;
    pb_hasBinary = true;
    pb_lastControlMs = pb_packetReceived;
    pb_controlFresh = true;
    portEXIT_CRITICAL(&pb_stateMux);
    pb_lastLineState.store(0);
}

static void pb_feedRxByte(uint8_t b) {
    pb_stream.feed(b, pb_packetReceived,
        [](const std::string &line) {
            const bool isHello = line.compare(0, 10, "PB2:HELLO:") == 0;
            if (isHello) {
                PBGamepad_resetControlState();
                pb_invokeEmergencyStop();
            }
            std::string reply = pb_protocol.line(line);
            pb_authenticated.store(pb_protocol.ready());
            if (!pb_protocol.ready() && !isHello) {
                PBGamepad_resetControlState();
                pb_invokeEmergencyStop();
            }
            if (!reply.empty()) PBGamepad_sendLine(String(reply.c_str()));
        },
        [](const uint8_t *data, size_t length, uint32_t started) {
            const uint32_t received = pb_packetReceived;
            pb_packetReceived = started;
            pb_tryParseBinary(data, length);
            pb_packetReceived = received;
        });
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

    pb_clearSession();
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
    pb_clearSession();
    pb_invokeEmergencyStop();
    Serial.print("[PB BLE] disconnected conn_id=");
    Serial.println(connId);
}

class PB_ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {}
    void onDisconnect(BLEServer *pServer) override {}

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        pb_enqueue(1, param->connect.conn_id);
    }

    void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        pb_enqueue(2, param->disconnect.conn_id, nullptr, 0, param->disconnect.reason);
    }
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        pb_enqueue(1, desc->conn_handle);
    }

    void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        pb_enqueue(2, desc->conn_handle);
    }
#endif
};

class PB_RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *) override {}
#if defined(CONFIG_BLUEDROID_ENABLED)
    void onWrite(BLECharacteristic *c, esp_ble_gatts_cb_param_t *param) override {
        pb_enqueue(3, param->write.conn_id, c->getData(), c->getLength());
    }
#endif
#if defined(CONFIG_NIMBLE_ENABLED)
    void onWrite(BLECharacteristic *c, ble_gap_conn_desc *desc) override {
        pb_enqueue(3, desc->conn_handle, c->getData(), c->getLength());
    }
#endif
};

static void pb_processEvents(void) {
    if (!pb_events) return;
    if (pb_queueFailed.exchange(false)) {
        Serial.println("[PB BLE] queue_overflow");
        // Fail closed; never authenticate using a stream with missing events.
        PB_Event dropped;
        for (int count = 0; count < 16 && xQueueReceive(pb_events, &dropped, 0) == pdTRUE; ++count) {
            if (dropped.type == 1) pb_disconnectConn(pbServer, dropped.conn);
        }
        if (pb_hasActiveConn) {
            const uint16_t conn = pb_activeConnId;
            pb_handleDisconnect(conn);
            pb_disconnectConn(pbServer, conn);
        } else {
            pb_invokeEmergencyStop();
            pb_clearSession();
        }
        pb_hasActiveConn = false;
        pb_deviceConnected = false;
        pb_activeConnId = PB_INVALID_CONN_ID;
        pb_advRetryPending = true;
        return;
    }
    PB_Event event;
    // Bound work so a busy peer cannot starve the user's motor loop.
    for (int count = 0; count < 16 && xQueueReceive(pb_events, &event, 0) == pdTRUE; ++count) {
        if (event.type == 1) pb_handleConnect(pbServer, event.conn);
        else if (event.type == 2) {
            Serial.print("[PB BLE] link_disconnected reason=");
            Serial.println(event.reason); // 65535: SDK callback has no reason.
            pb_handleDisconnect(event.conn);
        }
        else if (event.type == 3 && pb_hasActiveConn && event.conn == pb_activeConnId) {
            if ((uint32_t)(millis() - event.received) >= PB_CONTROL_TIMEOUT_MS) {
                Serial.print("[PB BLE] stale_event age_ms=");
                Serial.println((uint32_t)(millis() - event.received));
                // Do not leave the app connected with an invalidated session.
                // Clear local ownership now so queued old-link data is ignored.
                pb_handleDisconnect(event.conn);
                pb_disconnectConn(pbServer, event.conn);
                return;
            }
            pb_packetReceived = event.received;
            for (size_t i = 0; i < event.length; ++i) pb_feedRxByte(event.bytes[i]);
        }
    }
}

static void pb_serviceControlWatchdog(void) {
    if (!pb_hasActiveConn) return;
    const uint32_t age = PBGamepad_lastControlAgeMs();
    if (age == 0xFFFFFFFFUL || age < PB_CONTROL_TIMEOUT_MS) return;
    // Invalidate outputs before invoking board code. Keep protocol key and
    // sequence intact: only a newer authenticated packet can resume control.
    PBGamepad_resetControlState();
    pb_invokeEmergencyStop();
    Serial.print("[PB BLE] control_timeout age_ms=");
    Serial.println(age);
}

static void pb_controlWorkerTask(void *param) {
    (void)param;
    for (;;) {
        pb_processEvents();
        pb_serviceControlWatchdog();
        pb_tryRestartAdvertising();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void PBGamepad_init(const char *deviceName, const char *boardId, const char *pairCode) {
    if (pbServer) { Serial.println("[PB BLE] already_initialized"); return; }
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    const char *model = "ESP32-C3";
#else
    const char *model = "ESP32";
#endif
    if (!deviceName || !deviceName[0] || !boardId || !pairCode ||
        !pb_protocol.configure(boardId, pairCode, model)) {
        Serial.println("[PB BLE] invalid_configuration");
        return;
    }
    pb_events = xQueueCreate(16, sizeof(PB_Event));
    if (!pb_events) { Serial.println("[PB BLE] queue_allocation_failed"); return; }
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
    pb_advRetryPending = true;
    pb_startAdvertising(false);

    BaseType_t workerOk = xTaskCreate(
        pb_controlWorkerTask,
        "pb_ble_control",
        6144,
        nullptr,
        3,
        &pb_controlWorkerHandle
    );
    if (workerOk != pdPASS) {
        pb_controlWorkerHandle = nullptr;
        Serial.println("[PB BLE] control_worker_task_failed");
    }
}

String PBGamepad_getCommand(void) {
    PBGamepad_poll();
    return pb_lastLineState.load() ? String("0") : String("");
}

void PBGamepad_sendLine(const String &msg) {
    if (!pb_deviceConnected || pbTxChar == nullptr) return;

    String out = msg;
    out += '\n';
    // 20 bytes is valid even at the minimum ATT MTU (23).
    // Keep notification boundaries independent of protocol line boundaries.
    for (size_t offset = 0; offset < out.length(); offset += 20) {
        size_t length = std::min(size_t(20), size_t(out.length() - offset));
        pbTxChar->setValue(reinterpret_cast<uint8_t *>(&out[0]) + offset, length);
        pbTxChar->notify();
        delay(5);
    }
}

bool PBGamepad_isAuthenticated(void) {
    return pb_deviceConnected && pb_authenticated.load();
}

bool PBGamepad_isConnected(void) {
    return pb_deviceConnected;
}

bool PBGamepad_isControlFresh(void) {
    return pb_applyControlTimeout();
}

uint32_t PBGamepad_lastControlAgeMs(void) {
    bool fresh;
    uint32_t last;
    portENTER_CRITICAL(&pb_stateMux);
    fresh = pb_controlFresh;
    last = pb_lastControlMs;
    portEXIT_CRITICAL(&pb_stateMux);
    if (!fresh) {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(millis() - last);
}

void PBGamepad_poll(void) {
    if (pb_controlWorkerHandle == nullptr) {
        pb_processEvents();
        pb_serviceControlWatchdog();
        pb_tryRestartAdvertising();
    }
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
    bool binary;
    portENTER_CRITICAL(&pb_stateMux);
    binary = pb_hasBinary;
    portEXIT_CRITICAL(&pb_stateMux);
    return binary && pb_applyControlTimeout();
}

bool PBGamepad_readAxes(PBGamepadAxes *axes) {
    if (axes == nullptr) return false;
    bool binary;
    int8_t lx, ly, rx, ry;
    portENTER_CRITICAL(&pb_stateMux);
    binary = pb_hasBinary;
    const bool fresh = pb_controlFresh;
    const uint32_t last = pb_lastControlMs;
    lx = pb_lx; ly = pb_ly; rx = pb_rx; ry = pb_ry;
    portEXIT_CRITICAL(&pb_stateMux);
    const bool valid = binary && fresh && pb_authenticated.load() &&
        (uint32_t)(millis() - last) < PB_CONTROL_TIMEOUT_MS;
    if (!valid) {
        *axes = {0.0f, 0.0f, 0.0f, 0.0f};
        return false;
    }
    axes->lx = (float)lx / 100.0f;
    axes->ly = (float)ly / 100.0f;
    axes->rx = (float)rx / 100.0f;
    axes->ry = (float)ry / 100.0f;
    return true;
}

float PBGamepad_getLX(void) {
    PBGamepadAxes axes = {};
    PBGamepad_readAxes(&axes);
    return axes.lx;
}

float PBGamepad_getLY(void) {
    PBGamepadAxes axes = {};
    PBGamepad_readAxes(&axes);
    return axes.ly;
}

float PBGamepad_getRX(void) {
    PBGamepadAxes axes = {};
    PBGamepad_readAxes(&axes);
    return axes.rx;
}

float PBGamepad_getRY(void) {
    PBGamepadAxes axes = {};
    PBGamepad_readAxes(&axes);
    return axes.ry;
}

uint16_t PBGamepad_getButtons(void) {
    uint16_t buttons;
    portENTER_CRITICAL(&pb_stateMux);
    buttons = pb_controlFresh && (uint32_t)(millis() - pb_lastControlMs) < PB_CONTROL_TIMEOUT_MS
        ? pb_buttons : 0;
    portEXIT_CRITICAL(&pb_stateMux);
    return buttons;
}

int PBGamepad_getSpeed(void) {
    int speed;
    portENTER_CRITICAL(&pb_stateMux);
    speed = pb_controlFresh && (uint32_t)(millis() - pb_lastControlMs) < PB_CONTROL_TIMEOUT_MS
        ? pb_speed : -1;
    portEXIT_CRITICAL(&pb_stateMux);
    return speed;
}

#endif


