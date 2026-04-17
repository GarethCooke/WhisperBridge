#pragma once
#include <EspMqttBase.h>
#include <functional>

class MqttManager : public EspMqttBase {
public:
    using CommandCallback = std::function<void()>;

    // Build topics from deviceId and configure the MQTT client.
    void setup(const char* deviceId);

    // Publish ON or OFF to the state topic.
    void publishState(bool on);

    // Set the callback invoked when a boost command arrives over MQTT.
    void setCommandCallback(CommandCallback cb) { _commandCallback = cb; }

protected:
    void onConnected() override;
    void onMessage(const char* topic, const char* payload) override;

private:
    void publishDiscovery();

    String          _topicCommand;
    String          _topicState;
    String          _topicDiscovery;
    CommandCallback _commandCallback;
};

extern MqttManager Mqtt;
