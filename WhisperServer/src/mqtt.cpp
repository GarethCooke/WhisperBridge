#include "mqtt.h"
#include "config.h"
#include "state.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

MqttManager Mqtt;

// WiFiClient and PubSubClient kept file-scope to avoid pulling their headers into mqtt.h
static WiFiClient   s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);

// ── Private methods ───────────────────────────────────────────────────────────

void MqttManager::onMessage(const char* topic, uint8_t* payload, unsigned int length) {
    char msg[64] = {};
    memcpy(msg, payload, min((size_t)length, sizeof(msg) - 1));
    Serial.printf("[MQTT] %s → %s\n", topic, msg);

    if (strcmp(topic, Mqtt._topicCommand.c_str()) == 0) {
        if (parseBoostCommand(msg)) {
            Mqtt.publishState(true);  // optimistic ON
            if (Mqtt._commandCallback) Mqtt._commandCallback();
            // loop() will publish OFF once the BLE task completes
        }
        // Ignore "OFF" — boost is a one-shot action, not a toggle
    }
}

void MqttManager::publishDiscovery() {
    JsonDocument doc;
    String entityId      = String("whisperbridge_") + _deviceId;
    doc["name"]          = "Boost";
    doc["unique_id"]     = entityId + "_boost";
    doc["command_topic"] = _topicCommand;
    doc["state_topic"]   = _topicState;
    doc["payload_on"]    = MQTT_PAYLOAD_ON;
    doc["payload_off"]   = MQTT_PAYLOAD_OFF;
    doc["optimistic"]    = false;
    doc["retain"]        = false;
    doc["icon"]          = "mdi:fan";

    JsonObject dev        = doc["device"].to<JsonObject>();
    dev["name"]           = "WhisperBridge";
    dev["model"]          = "WhisperBridge v1";
    dev["manufacturer"]   = "Custom";
    dev["identifiers"][0] = entityId;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    if (!s_mqtt.publish(_topicDiscovery.c_str(), buf, /*retain=*/true)) {
        Serial.println("[MQTT] Discovery publish failed");
    } else {
        Serial.println("[MQTT] HA discovery published");
    }
}

void MqttManager::connect() {
    String clientId = String("whisperbridge-") + _deviceId;
    bool ok = (strlen(MQTT_USER) > 0)
        ? s_mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)
        : s_mqtt.connect(clientId.c_str());

    if (ok) {
        Serial.println("[MQTT] Connected");
        s_mqtt.subscribe(_topicCommand.c_str());
        publishDiscovery();
        publishState(false);
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", s_mqtt.state());
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void MqttManager::setup(const char* deviceId) {
    strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
    _topicCommand   = String("whisperbridge/") + _deviceId + "/boost";
    _topicState     = String("whisperbridge/") + _deviceId + "/boost/state";
    _topicDiscovery = String("homeassistant/switch/whisperbridge_") + _deviceId + "_boost/config";
    s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    s_mqtt.setCallback(onMessage);
    s_mqtt.setBufferSize(512);
}

void MqttManager::loop() {
    if (!s_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnect >= 5000) {
            _lastReconnect = now;
            connect();
        }
    } else {
        s_mqtt.loop();
    }
}

void MqttManager::publishState(bool on) {
    s_mqtt.publish(_topicState.c_str(), on ? MQTT_PAYLOAD_ON : MQTT_PAYLOAD_OFF, /*retain=*/true);
}

void MqttManager::setCommandCallback(CommandCallback cb) {
    _commandCallback = cb;
}
