# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

WhisperBridge is an ESP32 firmware that acts as a WiFi/MQTT-to-BLE bridge for a Vent-Axia Svara fan. Receiving an MQTT `ON` command triggers a BLE sequence (authenticate → send boost command) on the fan. The device exposes a small web UI and integrates with Home Assistant via MQTT auto-discovery as a `switch` entity.

## Build & flash commands

`pio` is not on PATH on Windows. Always invoke as:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" <args> -d WhisperServer
```

| Task | Command |
|---|---|
| Build | `pio run -e esp32dev -d WhisperServer` |
| Flash (USB) | `pio run -e esp32dev -t upload -d WhisperServer` |
| Flash (OTA) | `pio run -e esp32dev_ota -t upload -d WhisperServer` |
| Upload web UI (LittleFS) | `pio run -e esp32dev -t uploadfs -d WhisperServer` |
| Run native tests | `pio test -e native -d WhisperServer` |
| Serial monitor | `pio device monitor -d WhisperServer` |
| Start mock server | `cd WhisperClient/test-server && npm start` |

VS Code tasks mirror all of the above.

## Before flashing

Edit [WhisperServer/include/config.h](WhisperServer/include/config.h) and set:
- `MQTT_HOST` — broker IP address
- `FAN_MAC_ADDRESS` — target fan's Bluetooth MAC
- `OTA_PASSWORD` — OTA authentication password

## Architecture

### Firmware (WhisperServer/)

The firmware has three modules, each a singleton global (`Ble`, `Mqtt`):

**[main.cpp](WhisperServer/src/main.cpp)** — entry point. Handles WiFi (station vs AP), mDNS, OTA, and the AsyncWebServer REST endpoints. In AP/captive-portal mode only `setup.html` endpoints are active. In station mode the boost API and status endpoint are registered and `Mqtt.setup()` is called.

**[ble.cpp](WhisperServer/src/ble.cpp) / [ble.h](WhisperServer/include/ble.h)** — `BleBoost` class. BLE operations run in a dedicated FreeRTOS task (`ble_boost`, 8 KB stack) that blocks on a task notification. `Ble.trigger()` sends the notification; the task then connects to the fan, writes the PIN to the auth characteristic, then writes the boost payload to the command characteristic. `_running` and `_lastSuccess` are `std::atomic<bool>` so `main.cpp` can poll them safely from the loop task. NimBLE headers are kept out of `ble.h` via forward declarations.

**[mqtt.cpp](WhisperServer/src/mqtt.cpp) / [mqtt.h](WhisperServer/include/mqtt.h)** — `MqttManager` class wrapping PubSubClient. Subscribes to `whisperbridge/<id>/boost`, publishes state to `whisperbridge/<id>/boost/state`, and publishes HA auto-discovery to `homeassistant/switch/whisperbridge_<id>_boost/config`. `OFF` commands are intentionally ignored — boost is one-shot. `WiFiClient` and `PubSubClient` are file-scope statics to keep their headers out of `mqtt.h`.

**[state.h](WhisperServer/include/state.h)** — header-only `parseBoostCommand()`. Guards with `#ifndef ARDUINO` so it compiles on native for unit tests.

### Boot flow

1. `setup()` mounts LittleFS, derives device ID from last 3 MAC bytes
2. Calls `Ble.setup()` (starts FreeRTOS task)
3. Attempts WiFi station connect (credentials from NVS via `Preferences`)
4. If connected → `startStation()`: mDNS, OTA, REST API, `Mqtt.setup()`
5. If not → `startAP()`: captive portal at `10.0.0.1`, async WiFi scan, save-and-restart flow

### Web UI (WhisperServer/data/)

Plain HTML/JS, no frameworks (LittleFS size constraint). Two pages:
- `index.html` — shows IP, RSSI, running state, last BLE result; Boost button → `POST /api/boost`; polls `GET /api/status` every 2 s
- `setup.html` — WiFi credential form; polls `GET /api/networkdata` for available networks

### Mock server (WhisperClient/test-server/)

Node.js/Express server that mirrors the firmware REST API and serves the same `data/` files. Run with `npm start` at `http://localhost:3000` for UI development without hardware.

### Native tests (WhisperServer/test/)

Unity tests compiled for the `native` env. Currently cover `parseBoostCommand` from `state.h`. Tests live in `test/test_mqtt.cpp` and run on the host — no mocking of Arduino APIs needed because `state.h` is pure logic.

## Key libraries

| Library | Purpose |
|---|---|
| `h2zero/NimBLE-Arduino ^1.4.2` | BLE client (lighter than ESP-IDF BLE) |
| `knolleary/PubSubClient ^2.8` | MQTT with `setBufferSize(512)` |
| `bblanchon/ArduinoJson ^7.0.0` | `JsonDocument` (no capacity needed) |
| `esphome/ESPAsyncWebServer-esphome ^3.3.0` | Non-blocking web server |

## Important constraints

- Never call `delay()` or `ESP.restart()` from inside an `AsyncWebServer` callback — those run on the lwIP task. Schedule restarts via a flag polled in `loop()` (see `s_pendingRestart`).
- BLE operations block for hundreds of milliseconds; they must stay in the `ble_boost` FreeRTOS task, not in `loop()`.
- `STATUS_LED_PIN` in `config.h` is optional — comment it out to disable the status LED.
- OTA env uploads to `whisperbridge.local`; requires Windows Firewall to allow inbound for `python.exe` in the PlatformIO venv.
