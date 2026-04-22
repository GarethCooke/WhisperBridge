#include "ble.h"
#include "config.h"
#include <EspLogger.h>
#include <EspProvision.h>
#include <NimBLEDevice.h>

BleBoost Ble;

static std::vector<uint8_t> pinBytes() {
    String hex = Provision.getFanPin(BLE_PIN_HEX);
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < (size_t)hex.length(); i += 2) {
        char buf[3] = { hex[i], hex[i + 1], '\0' };
        bytes.push_back((uint8_t)strtoul(buf, nullptr, 16));
    }
    return bytes;
}

// ── BleBoost ──────────────────────────────────────────────────────────────────

void BleBoost::cleanup(NimBLEClient* client) {
    client->disconnect();
    NimBLEDevice::deleteClient(client);
}

NimBLERemoteCharacteristic* BleBoost::getChar(
        NimBLEClient* client,
        const char* svcUuid, const char* charUuid,
        const char* svcLabel, const char* charLabel) {
    NimBLERemoteService* svc = client->getService(svcUuid);
    if (!svc) { LOGF("[BLE] %s not found", svcLabel); return nullptr; }

    NimBLERemoteCharacteristic* ch = svc->getCharacteristic(charUuid);
    if (!ch) { LOGF("[BLE] %s not found", charLabel); return nullptr; }
    return ch;
}

bool BleBoost::writeChar(NimBLERemoteCharacteristic* ch,
                         const uint8_t* data, size_t len,
                         const char* label) {
    if (!ch->writeValue(data, len, true)) {
        LOGF("[BLE] Write failed: %s", label);
        return false;
    }
    LOGF("[BLE] Written: %s", label);
    return true;
}

void BleBoost::updateBoostStatus(NimBLERemoteCharacteristic* ch) {
    std::string val = ch->readValue();
    if (val.size() < 5) return;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(val.data());
    _boostOn      = (b[0] == 0x01);
    _boostRpm     = static_cast<uint16_t>(b[1] | (b[2] << 8));
    _boostSeconds = static_cast<uint16_t>(b[3] | (b[4] << 8));
    LOGF("[BLE] Fan: %s  RPM=%u  remaining=%us",
         _boostOn ? "ON" : "OFF", _boostRpm.load(), _boostSeconds.load());
}

bool BleBoost::pollBoostStatus() {
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(4);
    String fanMac = Provision.getFanMac(FAN_MAC_ADDRESS);

    if (!client->connect(NimBLEAddress(fanMac.c_str()))) {
        LOG("[BLE] Poll: connection failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteCharacteristic* authChar = getChar(client,
        BLE_AUTH_SERVICE_UUID, BLE_AUTH_CHAR_UUID,
        "Auth service", "Auth characteristic");
    if (!authChar) { cleanup(client); return false; }
    auto pin = pinBytes();
    if (!writeChar(authChar, pin.data(), pin.size(), "PIN")) { cleanup(client); return false; }
    vTaskDelay(pdMS_TO_TICKS(200));

    NimBLERemoteCharacteristic* cmdChar = getChar(client,
        BLE_CMD_SERVICE_UUID, BLE_CMD_CHAR_UUID,
        "Command service", "Command characteristic");
    if (!cmdChar) { cleanup(client); return false; }

    updateBoostStatus(cmdChar);
    cleanup(client);
    return true;
}

bool BleBoost::runSequence() {
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(4);  // 4 s — if in range it connects in <2 s
    String fanMac = Provision.getFanMac(FAN_MAC_ADDRESS);

    LOG("[BLE] Connecting...");
    if (!client->connect(NimBLEAddress(fanMac.c_str()))) {
        LOG("[BLE] Connection failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }
    LOG("[BLE] Connected");

    NimBLERemoteCharacteristic* authChar = getChar(client,
        BLE_AUTH_SERVICE_UUID, BLE_AUTH_CHAR_UUID,
        "Auth service", "Auth characteristic");
    if (!authChar) { cleanup(client); return false; }

    auto pin = pinBytes();
    if (!writeChar(authChar, pin.data(), pin.size(), "PIN")) { cleanup(client); return false; }
    vTaskDelay(pdMS_TO_TICKS(200));  // brief settle after auth

    NimBLERemoteCharacteristic* cmdChar = getChar(client,
        BLE_CMD_SERVICE_UUID, BLE_CMD_CHAR_UUID,
        "Command service", "Command characteristic");
    if (!cmdChar) { cleanup(client); return false; }

    if (!writeChar(cmdChar, BLE_BOOST_BYTES, sizeof(BLE_BOOST_BYTES), "Boost command")) { cleanup(client); return false; }

    // Read back immediately while still connected to capture initial countdown
    vTaskDelay(pdMS_TO_TICKS(200));
    updateBoostStatus(cmdChar);

    cleanup(client);
    return true;
}

void BleBoost::runTask() {
    for (;;) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(120000));
        _bleActive = true;
        if (notified) {
            _running = true;
            _lastSuccess = false;
            for (int attempt = 1; attempt <= 3 && !_lastSuccess; attempt++) {
                if (attempt > 1) {
                    LOGF("[BLE] Retry %d/3...", attempt);
                    // Longer gap between retries lets WiFi recover RF time
                    vTaskDelay(pdMS_TO_TICKS(6000));
                }
                _lastSuccess = runSequence();
            }
            // Let WiFi re-sync after the BLE RF activity before returning to idle
            vTaskDelay(pdMS_TO_TICKS(1500));
            _running = false;
            LOGF("[BLE] Sequence %s", _lastSuccess ? "succeeded" : "FAILED");
        } else {
            // Periodic poll — update fan boost state without triggering boost
            pollBoostStatus();
        }
        _bleActive = false;
    }
}

void BleBoost::taskEntry(void* param) {
    static_cast<BleBoost*>(param)->runTask();
}

void BleBoost::setup() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power — fan may be across the room or through a wall
    // Reduce scan duty cycle so WiFi gets adequate RF time during connection attempts
    NimBLEDevice::getScan()->setInterval(160);  // 100 ms
    NimBLEDevice::getScan()->setWindow(80);     // 50 ms (50% duty cycle)
    xTaskCreate(taskEntry, "ble_boost", 8192, this, 5,
                reinterpret_cast<TaskHandle_t*>(&_task));
    LOG("[BLE] Ready");
}

void BleBoost::trigger() {
    if (!_task || _running) return;
    xTaskNotifyGive(static_cast<TaskHandle_t>(_task));
}
