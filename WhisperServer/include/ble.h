#pragma once
#include <stdint.h>
#include <stddef.h>
#include <atomic>

// Forward declarations — full NimBLE headers stay in ble.cpp
class NimBLEClient;
class NimBLERemoteCharacteristic;

class BleBoost {
public:
    // Initialise NimBLE and start the background task.
    void setup();

    // Queue a boost sequence. No-op if one is already running.
    void trigger();

    bool isRunning()   const { return _running; }
    bool lastSuccess() const { return _lastSuccess; }

private:
    static void taskEntry(void* param);  // FreeRTOS entry — forwards to runTask()
    void        runTask();
    bool        runSequence();

    static void                        cleanup(NimBLEClient* client);
    static NimBLERemoteCharacteristic* getChar(NimBLEClient* client,
                                               const char* svcUuid, const char* charUuid,
                                               const char* svcLabel, const char* charLabel);
    static bool                        writeChar(NimBLERemoteCharacteristic* ch,
                                                 const uint8_t* data, size_t len,
                                                 const char* label);

    void*             _task        = nullptr;  // TaskHandle_t, opaque to keep FreeRTOS out of header
    std::atomic<bool> _running     { false };
    std::atomic<bool> _lastSuccess { false };
};

extern BleBoost Ble;
