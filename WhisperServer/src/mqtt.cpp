#include "mqtt.h"
#include "config.h"
#include "state.h"
#include <ArduinoJson.h>
#include <EspDevice.h>
#include <EspProvision.h>
#include <EspLogger.h>

MqttManager Mqtt;

void MqttManager::setup(const char* deviceId, const char* mqttUser, const char* mqttPass) {
    // _topicCommand = String("whisperbridge/") + deviceId + "/boost";
    // _topicState = String("whisperbridge/") + deviceId + "/boost/state";
    // _topicDiscovery = String("homeassistant/switch/whisperbridge_") + deviceId + "_boost/config";

    _topicCommand = "whisperbridge/boost";
    _topicState = "whisperbridge/boost/state";
    _topicDiscovery = "homeassistant/switch/whisperbridge_boost/config";

    LOGF("[MQTT] Topics for %s:", deviceId);
    LOGF("  Command:   %s", _topicCommand.c_str());
    LOGF("  State:     %s", _topicState.c_str());
    LOGF("  Discovery: %s", _topicDiscovery.c_str());

    String host = Provision.getMqttHost(MQTT_HOST);
    EspMqttBase::Config cfg;
    cfg.host = host.c_str();
    cfg.port = Provision.getMqttPort(MQTT_PORT);
    cfg.user = mqttUser;
    cfg.password = mqttPass;
    EspMqttBase::setup(cfg);
}

void MqttManager::publishDiscovery() {
    JsonDocument doc;
    String entityId = String("whisperbridge_") + EspDevice::id();

    LOGF("[MQTT] Publishing Home Assistant discovery for %s", entityId.c_str());

    doc["name"] = "Boost";
    doc["unique_id"] = entityId + "_boost";
    doc["command_topic"] = _topicCommand;
    doc["state_topic"] = _topicState;
    doc["payload_on"] = MQTT_PAYLOAD_ON;
    doc["payload_off"] = MQTT_PAYLOAD_OFF;
    doc["optimistic"] = false;
    doc["retain"] = false;
    doc["icon"] = "mdi:fan";

    LOGF("[MQTT]   name: Boost");
    LOGF("[MQTT]   unique_id: %s", (entityId + "_boost").c_str());
    LOGF("[MQTT]   command_topic: %s", _topicCommand.c_str());
    LOGF("[MQTT]   state_topic: %s", _topicState.c_str());
    LOGF("[MQTT]   payload_on: %s", MQTT_PAYLOAD_ON);
    LOGF("[MQTT]   payload_off: %s", MQTT_PAYLOAD_OFF);
    LOGF("[MQTT]   icon: mdi:fan");

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["name"] = "WhisperBridge";
    dev["model"] = "WhisperBridge v1";
    dev["manufacturer"] = "Custom";
    dev["identifiers"][0] = entityId;

    LOGF("[MQTT]   device.name: WhisperBridge");
    LOGF("[MQTT]   device.model: WhisperBridge v1");
    LOGF("[MQTT]   device.manufacturer: Custom");
    LOGF("[MQTT]   device.identifiers[0]: %s", entityId.c_str());

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
