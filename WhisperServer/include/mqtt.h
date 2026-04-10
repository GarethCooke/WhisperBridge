#pragma once
#include <Arduino.h>
#include <functional>

class MqttManager {
public:
    using CommandCallback = std::function<void()>;

    // Call once after WiFi connects. deviceId = last-3-MAC-bytes suffix.
    void setup(const char* deviceId);

    // Drive the MQTT client; call every loop iteration.
    void loop();

    // Publish ON or OFF to the state topic.
    void publishState(bool on);

    // Set the callback invoked when a boost command arrives over MQTT.
    void setCommandCallback(CommandCallback cb);

private:
    void        connect();
    void        publishDiscovery();
    static void onMessage(const char* topic, uint8_t* payload, unsigned int length);

    char            _deviceId[7]  = {};  // 6 hex chars + null, set once in setup()
    String          _topicCommand;
    String          _topicState;
    String          _topicDiscovery;
    CommandCallback _commandCallback;
    unsigned long   _lastReconnect = 0;
};

extern MqttManager Mqtt;
