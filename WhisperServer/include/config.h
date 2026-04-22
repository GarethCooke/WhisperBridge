#pragma once
#include <stdint.h>

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_AP_PASSWORD "whisperbridge"  // min 8 chars for WPA2

// ── MQTT ─────────────────────────────────────────────────────────────────────
// Edit broker IP before flashing. User/password are set at runtime via /settings.html.
#define MQTT_HOST     "192.168.x.x"   // TODO: set broker IP
constexpr uint16_t MQTT_PORT = 1883;

// Payloads
#define MQTT_PAYLOAD_ON  "ON"
#define MQTT_PAYLOAD_OFF "OFF"

// ── BLE – Vent-Axia Svara ────────────────────────────────────────────────────
#define FAN_MAC_ADDRESS "xx:xx:xx:xx:xx:xx"

// Service containing the PIN/auth characteristic
#define BLE_AUTH_SERVICE_UUID "e6834e4b-7b3a-48e6-91e4-f1d005f564d3"
#define BLE_AUTH_CHAR_UUID    "4cad343a-209a-40b7-b911-4d9b3df569b2"
// PIN fallback — overridden at runtime via /settings.html (stored in NVS as hex string)
#define BLE_PIN_HEX "e3146205"

// Service containing the command characteristic
#define BLE_CMD_SERVICE_UUID  "c119e858-0531-4681-9674-5a11f0e53bb4"
#define BLE_CMD_CHAR_UUID     "118c949c-28c8-4139-b0b3-36657fd055a9"
// Boost command payload
constexpr uint8_t BLE_BOOST_BYTES[] = { 0x01, 0x60, 0x09, 0x84, 0x03 };

// ── Optional status LED ──────────────────────────────────────────────────────
// HIGH while BLE boost sequence is running. Comment out to disable.
#define STATUS_LED_PIN 2

// ── Optional reset button ────────────────────────────────────────────────────
// Hold for 5 s (any mode) to clear WiFi credentials and restart into AP setup.
// GPIO0 is the BOOT button on most ESP32 dev boards. Comment out to disable.
// #define RESET_BUTTON_PIN 0

// ── OTA ──────────────────────────────────────────────────────────────────────
// Default used on first boot; override at runtime via /settings.html.
#define OTA_PASSWORD "password"
