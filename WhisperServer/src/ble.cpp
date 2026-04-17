#include "ble.h"
#include "config.h"
#include <NimBLEDevice.h>

BleBoost Ble;

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
    if (!svc) { Serial.printf("[BLE] %s not found\n", svcLabel); return nullptr; }

    NimBLERemoteCharacteristic* ch = svc->getCharacteristic(charUuid);
    if (!ch) { Serial.printf("[BLE] %s not found\n", charLabel); return nullptr; }
    return ch;
}

bool BleBoost::writeChar(NimBLERemoteCharacteristic* ch,
                         const uint8_t* data, size_t len,
                         const char* label) {
    if (!ch->writeValue(data, len, true)) {
        Serial.printf("[BLE] Write failed: %s\n", label);
        return false;
    }
    Serial.printf("[BLE] Written: %s\n", label);
    return true;
}

bool BleBoost::runSequence() {
    NimBLEClient* client = NimBLEDevice::createClient();

    Serial.println("[BLE] Connecting...");
    if (!client->connect(NimBLEAddress(FAN_MAC_ADDRESS))) {
        Serial.println("[BLE] Connection failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }
    Serial.println("[BLE] Connected");

    NimBLERemoteCharacteristic* authChar = getChar(client,
        BLE_AUTH_SERVICE_UUID, BLE_AUTH_CHAR_UUID,
        "Auth service", "Auth characteristic");
    if (!authChar) { cleanup(client); return false; }

    if (!writeChar(authChar, BLE_PIN_BYTES, sizeof(BLE_PIN_BYTES), "PIN")) { cleanup(client); return false; }
    vTaskDelay(pdMS_TO_TICKS(200));  // brief settle after auth

    NimBLERemoteCharacteristic* cmdChar = getChar(client,
        BLE_CMD_SERVICE_UUID, BLE_CMD_CHAR_UUID,
        "Command service", "Command characteristic");
    if (!cmdChar) { cleanup(client); return false; }

    if (!writeChar(cmdChar, BLE_BOOST_BYTES, sizeof(BLE_BOOST_BYTES), "Boost command")) { cleanup(client); return false; }

    cleanup(client);
    return true;
}

void BleBoost::runTask() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        _running = true;
        _lastSuccess = false;
        for (int attempt = 1; attempt <= 3 && !_lastSuccess; attempt++) {
            if (attempt > 1) {
                Serial.printf("[BLE] Retry %d/3...\n", attempt);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            _lastSuccess = runSequence();
        }
        _running = false;
        Serial.printf("[BLE] Sequence %s\n", _lastSuccess ? "succeeded" : "FAILED");
    }
}

void BleBoost::taskEntry(void* param) {
    static_cast<BleBoost*>(param)->runTask();
}

void BleBoost::setup() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power — fan may be across the room or through a wall
    xTaskCreate(taskEntry, "ble_boost", 8192, this, 5,
                reinterpret_cast<TaskHandle_t*>(&_task));
    Serial.println("[BLE] Ready");
}

void BleBoost::trigger() {
    if (!_task || _running) return;
    xTaskNotifyGive(static_cast<TaskHandle_t>(_task));
}
