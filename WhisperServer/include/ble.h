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

    bool     isRunning()    const { return _running; }
    bool     isActive()     const { return _bleActive; }
    bool     lastSuccess()  const { return _lastSuccess; }

    // Last known fan state, decoded from the boost characteristic.
    bool     boostOn()      const { return _boostOn; }
    uint16_t boostRpm()     const { return _boostRpm; }
    uint16_t boostSeconds() const { return _boostSeconds; }

private:
    static void taskEntry(void* param);  // FreeRTOS entry — forwards to runTask()
    void        runTask();
    bool        runSequence();
    bool        pollBoostStatus();
    void        updateBoostStatus(NimBLERemoteCharacteristic* ch);

    static void                        cleanup(NimBLEClient* client);
    static NimBLERemoteCharacteristic* getChar(NimBLEClient* client,
                                               const char* svcUuid, const char* charUuid,
                                               const char* svcLabel, const char* charLabel);
    static bool                        writeChar(NimBLERemoteCharacteristic* ch,
                                                 const uint8_t* data, size_t len,
                                                 const char* label);

    void*              _task        = nullptr;  // TaskHandle_t, opaque to keep FreeRTOS out of header
    std::atomic<bool>  _running     { false };
    std::atomic<bool>  _bleActive   { false };
    std::atomic<bool>  _lastSuccess { false };
    std::atomic<bool>  _boostOn     { false };
    std::atomic<uint16_t> _boostRpm     { 0 };
    std::atomic<uint16_t> _boostSeconds { 0 };
};

extern BleBoost Ble;
