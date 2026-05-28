#include "LineFollowerRobot.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ESP_PLATFORM)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char *LFR_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *LFR_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *LFR_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLEServer         *lfrServer = nullptr;
static BLECharacteristic *lfrTxChar = nullptr;
static BLECharacteristic *lfrRxChar = nullptr;
static BLEAdvertising    *lfrAdv    = nullptr;

static volatile bool lfrConnected = false;
static volatile bool lfrHasActiveConn = false;
static const uint16_t LFR_INVALID_CONN_ID = 0xFFFF;
static volatile uint16_t lfrActiveConnId = LFR_INVALID_CONN_ID;
static uint32_t lfrRejectedConnCount = 0;
static String lfrRxBuffer;
static String lfrBuf;

static const int LFR_QUEUE_SIZE = 6;
static String lfrQueue[LFR_QUEUE_SIZE];
static int lfrQHead = 0;
static int lfrQTail = 0;
static int lfrQCount = 0;

static void lfrQueueLine(const String &line) {
    if (lfrQCount >= LFR_QUEUE_SIZE) {
        lfrQHead = (lfrQHead + 1) % LFR_QUEUE_SIZE;
        lfrQCount--;
    }
    lfrQueue[lfrQTail] = line;
    lfrQTail = (lfrQTail + 1) % LFR_QUEUE_SIZE;
    lfrQCount++;
}

static void lfrStartAdvertising() {
    if (!lfrAdv || lfrHasActiveConn || lfrConnected) return;
    lfrAdv->start();
}

static void lfrStopAdvertising() {
    if (lfrAdv) lfrAdv->stop();
}

static void lfrDisconnectConn(BLEServer *pServer, uint16_t connId) {
    if (pServer == nullptr) return;
#if defined(CONFIG_NIMBLE_ENABLED)
    pServer->disconnect(connId, BLE_ERR_REM_USER_CONN_TERM);
#elif defined(CONFIG_BLUEDROID_ENABLED)
    pServer->disconnect(connId);
#else
    (void)connId;
#endif
}

static void lfrAcceptOwner(uint16_t connId) {
    lfrHasActiveConn = true;
    lfrActiveConnId = connId;
    lfrConnected = true;
    lfrStopAdvertising();
    Serial.print("[LFR BLE] connected conn_id=");
    Serial.println(connId);
}

static void lfrHandleConnect(BLEServer *pServer, uint16_t connId) {
    if (lfrHasActiveConn && lfrActiveConnId != connId) {
        lfrRejectedConnCount++;
        Serial.print("[LFR BLE] duplicate_rejected conn_id=");
        Serial.print(connId);
        Serial.print(" active_conn_id=");
        Serial.print(lfrActiveConnId);
        Serial.print(" count=");
        Serial.println(lfrRejectedConnCount);
        lfrDisconnectConn(pServer, connId);
        return;
    }
    lfrAcceptOwner(connId);
}

static void lfrHandleDisconnect(uint16_t connId) {
    if (!lfrHasActiveConn || lfrActiveConnId != connId) {
        Serial.print("[LFR BLE] disconnect_ignored conn_id=");
        Serial.print(connId);
        Serial.print(" active_conn_id=");
        Serial.println(lfrActiveConnId);
        return;
    }

    lfrHasActiveConn = false;
    lfrActiveConnId = LFR_INVALID_CONN_ID;
    lfrConnected = false;
    Serial.print("[LFR BLE] disconnected conn_id=");
    Serial.println(connId);
    lfrStartAdvertising();
}

class LFR_ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {
#if !defined(CONFIG_BLUEDROID_ENABLED) && !defined(CONFIG_NIMBLE_ENABLED)
        if (!lfrHasActiveConn) {
            lfrAcceptOwner(LFR_INVALID_CONN_ID);
        } else {
            Serial.println("[LFR BLE] duplicate_rejected conn_id=unknown");
            if (pServer) pServer->disconnect(0);
        }
#else
        (void)pServer;
#endif
    }

    void onDisconnect(BLEServer *pServer) override {
#if !defined(CONFIG_BLUEDROID_ENABLED) && !defined(CONFIG_NIMBLE_ENABLED)
        (void)pServer;
        lfrHasActiveConn = false;
        lfrActiveConnId = LFR_INVALID_CONN_ID;
        lfrConnected = false;
        Serial.println("[LFR BLE] disconnected conn_id=unknown");
        lfrStartAdvertising();
#else
        (void)pServer;
#endif
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        lfrHandleConnect(pServer, param->connect.conn_id);
    }

    void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        (void)pServer;
        lfrHandleDisconnect(param->disconnect.conn_id);
    }
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        lfrHandleConnect(pServer, desc->conn_handle);
    }

    void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
        (void)pServer;
        lfrHandleDisconnect(desc->conn_handle);
    }
#endif
};

class LFR_RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        const uint8_t *data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();
        if (len == 0) return;

        for (size_t i = 0; i < len; ++i) {
            lfrRxBuffer += (char)data[i];
        }
    }
};

static void lfrProcessRx() {
    while (lfrRxBuffer.length() > 0) {
        char ch = lfrRxBuffer[0];
        lfrRxBuffer.remove(0, 1);

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            String line = lfrBuf;
            lfrBuf = "";
            line.trim();
            if (line.length() == 0) continue;

            if (line == "PING") {
                LFR_sendLine("PONG");
                continue;
            }
            lfrQueueLine(line);
        } else {
            lfrBuf += ch;
            if (lfrBuf.length() > 256) {
                lfrBuf.remove(0, lfrBuf.length() - 256);
            }
        }
    }
}

void LFR_begin(const char* deviceName) {
    Serial.println("[LFR BLE] init start");
    lfrHasActiveConn = false;
    lfrActiveConnId = LFR_INVALID_CONN_ID;
    lfrConnected = false;

    BLEDevice::init(deviceName);

    lfrServer = BLEDevice::createServer();
#if defined(CONFIG_BLUEDROID_ENABLED)
    lfrServer->advertiseOnDisconnect(false);
#endif
    lfrServer->setCallbacks(new LFR_ServerCallbacks());

    BLEService *svc = lfrServer->createService(LFR_SERVICE_UUID);

    lfrTxChar = svc->createCharacteristic(
        LFR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
#if defined(CONFIG_BLUEDROID_ENABLED)
    lfrTxChar->addDescriptor(new BLE2902());
#endif

    lfrRxChar = svc->createCharacteristic(
        LFR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    lfrRxChar->setCallbacks(new LFR_RxCallbacks());

    svc->start();

    lfrAdv = BLEDevice::getAdvertising();
    lfrAdv->addServiceUUID(LFR_SERVICE_UUID);
    lfrAdv->setScanResponse(true);
    lfrAdv->setMinPreferred(0x06);
    lfrAdv->setMinPreferred(0x12);
    lfrAdv->start();

    Serial.println("[LFR BLE] Advertising started");
}

void LFR_poll() {
    lfrProcessRx();
}

bool LFR_isConnected() {
    return lfrConnected;
}

bool LFR_available() {
    lfrProcessRx();
    return lfrQCount > 0;
}

bool LFR_readLine(String &out) {
    lfrProcessRx();
    if (lfrQCount == 0) return false;
    out = lfrQueue[lfrQHead];
    lfrQHead = (lfrQHead + 1) % LFR_QUEUE_SIZE;
    lfrQCount--;
    return true;
}

void LFR_sendLine(const String &msg) {
    if (!lfrConnected || lfrTxChar == nullptr) return;

    String out = msg;
    out += '\n';
    lfrTxChar->setValue(out.c_str());
    lfrTxChar->notify();

    Serial.print("[LFR BLE TX] ");
    Serial.println(msg);
}

#else
void LFR_begin(const char*) {}
void LFR_poll() {}
bool LFR_isConnected() { return false; }
bool LFR_available() { return false; }
bool LFR_readLine(String &) { return false; }
void LFR_sendLine(const String &) {}
#endif
