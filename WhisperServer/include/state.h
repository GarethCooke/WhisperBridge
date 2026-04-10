#pragma once

// Allow compiling on a native (non-Arduino) host for unit testing
#ifndef ARDUINO
#include <cstring>
#endif

#include "config.h"

// Returns true if the MQTT payload should trigger a boost sequence.
// Accepts MQTT_PAYLOAD_ON ("ON") or the numeric "1".
inline bool parseBoostCommand(const char* msg) {
    return strcmp(msg, MQTT_PAYLOAD_ON) == 0 || strcmp(msg, "1") == 0;
}
