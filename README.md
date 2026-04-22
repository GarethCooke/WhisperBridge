# WhisperBridge

WiFi/MQTT-to-BLE bridge — ESP32 firmware that lets Home Assistant control a Vent-Axia Svara fan via a one-shot boost command, no cloud subscription required.

## Project Summary

WhisperBridge is an ESP32 firmware that acts as a protocol bridge between a home automation network and a Vent-Axia Svara bathroom fan. The fan exposes a proprietary BLE GATT interface — it has no WiFi or MQTT support. WhisperBridge fills that gap: it connects to the home network, subscribes to an MQTT topic, and translates each `ON` command into the correct BLE authentication and boost command sequence targeted at the fan by MAC address.

On first boot with no stored WiFi credentials, the device starts a WPA2 captive-portal access point (`WhisperBridge-Setup`) at `10.0.0.1`. The user connects, scans nearby networks, picks one, and saves credentials — the device then restarts in station mode. In station mode it registers as `whisperbridge.local` via mDNS, starts ArduinoOTA, and publishes Home Assistant MQTT auto-discovery so the fan appears automatically as a switch entity with the `mdi:fan` icon.

The BLE authentication and boost sequence blocks for hundreds of milliseconds. Rather than stalling the main loop, this work runs in a dedicated FreeRTOS task (`ble_boost`, 8 KB stack). The main loop posts a task notification to trigger a run; the BLE task atomically updates `_running` and `_lastSuccess` flags that the loop reads safely without locks. Once the task completes, the loop detects the falling edge on `_running` and publishes `OFF` to the state topic to keep Home Assistant in sync.

## Project Structure

```text
WhisperBridge/
├── WhisperServer/              # ESP32 firmware (C++20, PlatformIO)
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp            # WiFi (STA/AP), mDNS, OTA, REST API, loop
│   │   ├── ble.cpp             # BleBoost class — FreeRTOS task, GATT client
│   │   └── mqtt.cpp            # MqttManager — PubSubClient, HA discovery
│   ├── include/
│   │   ├── config.h            # MQTT host, fan MAC, OTA password — edit before flashing
│   │   ├── ble.h               # BleBoost interface (NimBLE headers kept out via fwd decls)
│   │   ├── mqtt.h              # MqttManager interface
│   │   └── state.h             # Pure logic: parseBoostCommand — #ifndef ARDUINO for host tests
│   ├── data/                   # LittleFS web UI (served from device flash)
│   │   ├── index.html          # Status + Boost button; polls GET /api/status every 2 s
│   │   └── setup.html          # WiFi credential form; polls GET /api/networkdata
│   └── test/
│       └── test_mqtt.cpp       # Unity tests for parseBoostCommand (runs on host)
└── WhisperClient/
    └── test-server/            # Node.js mock server for UI development without hardware
        ├── server.js
        └── ...
```

## API

| Method | Endpoint            | Body              | Description                                      |
| ------ | ------------------- | ----------------- | ------------------------------------------------ |
| GET    | `/api/status`       | —                 | Returns `{"connected": bool, "running": bool, "lastSuccess": bool}` |
| POST   | `/api/boost`        | —                 | Triggers a BLE boost sequence on the fan         |
| GET    | `/api/networkdata`  | —                 | Returns available WiFi networks (async scan)     |
| POST   | `/api/networkset`   | `{"ssid":"","password":""}` | Saves WiFi credentials and restarts    |

MQTT topics (where `<id>` = last 3 bytes of MAC):

| Topic | Direction | Payload |
| ----- | --------- | ------- |
| `whisperbridge/<id>/boost` | Subscribe | `ON` triggers boost; `OFF` ignored |
| `whisperbridge/<id>/boost/state` | Publish | `ON` while running, `OFF` when done |
| `homeassistant/switch/whisperbridge_<id>_boost/config` | Publish (on connect) | HA MQTT auto-discovery payload |

## System Setup

For full system setup — Home Assistant, Mosquitto, Cloudflare Tunnel, and the Alexa/Lambda integration — see [SETUP.md](SETUP.md).

## Before Flashing

Edit [`WhisperServer/include/config.h`](WhisperServer/include/config.h) and set:

- `MQTT_HOST` — broker IP address
- `FAN_MAC_ADDRESS` — target fan's Bluetooth MAC (format: `"AA:BB:CC:DD:EE:FF"`)
- `OTA_PASSWORD` — OTA authentication password

## Build & Flash

`pio` is not on PATH on Windows. Always invoke via:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" <args> -d WhisperServer
```

| Task                  | Command                                                   |
| --------------------- | --------------------------------------------------------- |
| Build                 | `pio run -e esp32dev -d WhisperServer`                    |
| Flash (USB)           | `pio run -e esp32dev -t upload -d WhisperServer`          |
| Flash (OTA)           | `pio run -e esp32dev_ota -t upload -d WhisperServer`      |
| Upload web UI (LittleFS) | `pio run -e esp32dev -t uploadfs -d WhisperServer`     |
| Serial monitor        | `pio device monitor -d WhisperServer`                     |

## Running Tests

### WhisperServer — C++ firmware (Unity via PlatformIO)

Requires `gcc`/`g++` on your PATH (install MinGW-w64 if not present).

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native -d WhisperServer
```

### Mock server — Node.js (for UI development)

```powershell
cd WhisperClient\test-server
npm install   # first time only
npm start
```

Then open [http://localhost:3000](http://localhost:3000) in a browser.

## Design Decisions

| Decision | Choice | Reason |
| -------- | ------ | ------ |
| MCU | ESP32 DevKit V1 | Dual-core, WiFi + BLE built in |
| BLE stack | NimBLE (`h2zero/NimBLE-Arduino`) | Significantly less flash/RAM than default ESP32 BLE stack |
| BLE execution | Dedicated FreeRTOS task (`ble_boost`, 8 KB stack) | BLE sequence blocks for seconds — keeps Arduino loop free for MQTT, OTA, REST |
| MQTT | `knolleary/PubSubClient` + `setBufferSize(512)` | Lightweight, well-maintained |
| HA integration | MQTT auto-discovery | Device appears automatically in Home Assistant as a switch entity |
| WiFi setup | Captive-portal AP on first boot | No hardcoded credentials; AP at `10.0.0.1`, WPA2 |
| OTA updates | ArduinoOTA on `whisperbridge.local:3232` | Wireless reflashing without USB access |
| Restart safety | `s_pendingRestart` flag polled in `loop()` | `ESP.restart()` inside an AsyncWebServer callback crashes the device |
| Control model | `OFF` MQTT commands ignored | Boost is one-shot; the bridge publishes `OFF` itself when the BLE task finishes |
