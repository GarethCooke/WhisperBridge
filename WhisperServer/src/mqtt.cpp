#include "mqtt.h"
#include "config.h"
#include "state.h"
#include <ArduinoJson.h>
#include <EspDevice.h>

MqttManager Mqtt;

void MqttManager::setup(const char* deviceId, const char* mqttUser, const char* mqttPass) {
    _topicCommand   = String("whisperbridge/") + deviceId + "/boost";
    _topicState     = String("whisperbridge/") + deviceId + "/boost/state";
    _topicDiscovery = String("homeassistant/switch/whisperbridge_") + deviceId + "_boost/config";

    EspMqttBase::Config cfg;
    cfg.host     = MQTT_HOST;
    cfg.port     = MQTT_PORT;
    cfg.user     = mqttUser;
    cfg.password = mqttPass;
    EspMqttBase::setup(cfg);
}

void MqttManager::publishDiscovery() {
    JsonDocument doc;
    String entityId      = String("whisperbridge_") + EspDevice::id();
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
    publish(_topicDiscovery.c_str(), buf, /*retain=*/true);
}

void MqttManager::onConnected() {
    subscribe(_topicCommand.c_str());
    publishDiscovery();
    publishState(false);
}

void MqttManager::onMessage(const char* topic, const char* payload) {
    if (strcmp(topic, _topicCommand.c_str()) != 0) return;
    if (!parseBoostCommand(payload)) return;
    publishState(true);
    if (_commandCallback) _commandCallback();
}

void MqttManager::publishState(bool on) {
    publish(_topicState.c_str(), on ? MQTT_PAYLOAD_ON : MQTT_PAYLOAD_OFF, /*retain=*/true);
}
