#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <EspDevice.h>
#include <EspProvision.h>

#include "config.h"
#include "ble.h"
#include "mqtt.h"

void setup() {
    Serial.begin(115200);
    delay(500);

#ifdef STATUS_LED_PIN
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
#endif
#ifdef RESET_BUTTON_PIN
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
#endif

    Ble.setup();

    Provision.onStation([](AsyncWebServer& srv) {
        srv.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["running"] = Ble.isRunning();
            doc["ble_ok"]  = Ble.lastSuccess();
            doc["ssid"]    = WiFi.SSID();
            doc["ip"]      = WiFi.localIP().toString();
            doc["rssi"]    = WiFi.RSSI();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        srv.on("/api/boost", HTTP_POST, [](AsyncWebServerRequest* req) {
            if (Ble.isRunning()) {
                req->send(409, "application/json", "{\"error\":\"already running\"}");
                return;
            }
            Ble.trigger();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        srv.on("/api/deviceinfo", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["id"]   = EspDevice::id();
            doc["name"] = EspDevice::name();
            doc["url"]  = EspDevice::url();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        srv.on("/api/networkset", HTTP_POST,
               [](AsyncWebServerRequest* req) {},
               nullptr,
               [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t total) {
                   if (len < total) return;
                   JsonDocument doc;
                   if (deserializeJson(doc, (char*)data, len) != DeserializationError::Ok) {
                       req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
                       return;
                   }
                   const char* ssid = doc["ssid"] | "";
                   if (strlen(ssid) == 0) {
                       req->send(400, "application/json", "{\"error\":\"ssid required\"}");
                       return;
                   }
                   req->send(200, "application/json", "{\"ok\":true}");
                   Provision.saveCredentialsAndRestart(ssid, doc["password"] | "");
               });

        srv.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

        Mqtt.setCommandCallback([]() { Ble.trigger(); });
        String mqttUser = Provision.getMqttUser();
        String mqttPass = Provision.getMqttPass();
        Mqtt.setup(EspDevice::id().c_str(), mqttUser.c_str(), mqttPass.c_str());
    });

    EspProvisionConfig cfg;
    cfg.apPassword       = WIFI_AP_PASSWORD;
    cfg.nvsNamespace     = "whisper";
    cfg.otaPassword      = OTA_PASSWORD;
    cfg.friendlyHostname = "whisperbridge";
    cfg.serviceType      = nullptr;
    Provision.begin("whisperbridge", "WhisperBridge", cfg);
}

void loop() {
    Provision.loop();

    if (Provision.isApMode()) return;

    Mqtt.loop();

    static bool s_wasRunning = false;
    const bool  running      = Ble.isRunning();
    if (s_wasRunning && !running) {
        Mqtt.publishState(false);
    }
    s_wasRunning = running;

#ifdef STATUS_LED_PIN
    digitalWrite(STATUS_LED_PIN, running ? HIGH : LOW);
#endif

#ifdef RESET_BUTTON_PIN
    static unsigned long s_btnPressedAt = 0;
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (s_btnPressedAt == 0) s_btnPressedAt = millis();
        else if (millis() - s_btnPressedAt >= 5000) {
            Serial.println("[Reset] Button held 5 s — clearing credentials");
            Provision.factoryReset();
            s_btnPressedAt = 0;
        }
    } else {
        s_btnPressedAt = 0;
    }
#endif
}
