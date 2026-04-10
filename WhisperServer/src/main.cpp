#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <map>
#include <string>
#include "config.h"
#include "ble.h"
#include "mqtt.h"

static Preferences    s_prefs;
static AsyncWebServer s_server(80);
static DNSServer      s_dns;
static bool           s_apMode        = false;
static volatile bool  s_pendingRestart = false;
static String         s_deviceId;  // set once in setup()

static constexpr char NVS_NAMESPACE[] = "whisper";
static constexpr char NVS_KEY_SSID[]  = "ssid";
static constexpr char NVS_KEY_PASS[]  = "pass";

// ── WiFi scan (AP mode only) ───────────────────────────────────────────────────

static int  s_scanCount = WIFI_SCAN_FAILED;
static bool s_scanReady = false;

static void pollScan() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n >= 0) {
        s_scanCount = n;
        s_scanReady = true;
    } else {
        s_scanReady = false;
        WiFi.scanNetworks(/*async=*/true);
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static String getDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    // mac[0..5] = most- to least-significant; use last 3 bytes for a short unique suffix
    char id[7];  // 6 hex chars + null terminator
    snprintf(id, sizeof(id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(id);
}

static void factoryReset() {
    Serial.println("[Reset] Clearing credentials — restarting into AP mode");
    s_prefs.begin(NVS_NAMESPACE, false);
    s_prefs.remove(NVS_KEY_SSID);
    s_prefs.remove(NVS_KEY_PASS);
    s_prefs.end();
    s_pendingRestart = true;
}

// ── Common endpoints (registered in both AP and station mode) ─────────────────

static void registerCommonEndpoints() {
    s_server.on("/api/deviceinfo", HTTP_GET, [](AsyncWebServerRequest* req) {
        String json = "{\"id\":\"" + s_deviceId + "\",\"url\":\"http://" WIFI_HOSTNAME ".local\"}";
        req->send(200, "application/json", json);
    });
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

static bool connectWifi() {
    s_prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    String ssid = s_prefs.getString(NVS_KEY_SSID, "");
    String pass = s_prefs.getString(NVS_KEY_PASS, "");
    s_prefs.end();

    if (ssid.isEmpty()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(ssid.c_str(), pass.c_str());

    Serial.printf("[WiFi] Connecting to %s", ssid.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

// ── AP / captive-portal mode ──────────────────────────────────────────────────

static void startAP() {
    const IPAddress apIp(10, 0, 0, 1);
    s_apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    s_dns.start(53, "*", apIp);
    Serial.printf("[WiFi] AP started: %s  IP: 10.0.0.1\n", WIFI_AP_SSID);

    registerCommonEndpoints();

    // Available networks (async scan, polled from loop)
    s_server.on("/api/networkdata", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!s_scanReady) {
            req->send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
            return;
        }

        // Deduplicate by SSID, keep strongest signal per network
        struct NetInfo { int rssi; bool secure; };
        std::map<std::string, NetInfo> best;
        for (int i = 0; i < s_scanCount; i++) {
            std::string ssid = WiFi.SSID(i).c_str();
            if (ssid.empty()) continue;
            int  rssi   = WiFi.RSSI(i);
            bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            auto it = best.find(ssid);
            if (it == best.end() || rssi > it->second.rssi)
                best[ssid] = {rssi, secure};
        }

        // Sort strongest first
        std::vector<std::pair<std::string, NetInfo>> sorted(best.begin(), best.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second.rssi > b.second.rssi; });

        JsonDocument doc;
        doc["scanning"] = false;
        JsonArray nets = doc["networks"].to<JsonArray>();
        for (const auto& [ssid, info] : sorted) {
            JsonObject n = nets.add<JsonObject>();
            n["ssid"]   = ssid;
            n["rssi"]   = info.rssi;
            n["secure"] = info.secure;
        }

        // Kick off a fresh scan for the next request
        s_scanReady = false;
        WiFi.scanDelete();
        WiFi.scanNetworks(/*async=*/true);

        String output;
        serializeJson(doc, output);
        req->send(200, "application/json", output);
    });

    // Save credentials — schedule restart via loop() to avoid calling delay/restart
    // from within an AsyncWebServer callback (runs on the lwIP task, not loopTask)
    s_server.on("/api/networkset", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok ||
                !doc["ssid"].is<const char*>()) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            s_prefs.begin(NVS_NAMESPACE, false);
            s_prefs.putString(NVS_KEY_SSID, doc["ssid"].as<String>());
            s_prefs.putString(NVS_KEY_PASS, doc["password"] | "");
            s_prefs.end();
            req->send(200, "application/json", "{\"ok\":true}");
            s_pendingRestart = true;
        }
    );

    s_server.onNotFound([](AsyncWebServerRequest* req) {
        req->redirect("http://10.0.0.1/setup.html");
    });

    s_server.serveStatic("/", LittleFS, "/");
    s_server.begin();

    WiFi.scanNetworks(/*async=*/true);  // kick off first scan
}

// ── Station mode ──────────────────────────────────────────────────────────────

static void startStation() {
    Serial.printf("[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());

    MDNS.begin(WIFI_HOSTNAME);
    MDNS.addService("http", "tcp", 80);

    ArduinoOTA.setHostname(WIFI_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]()  { Serial.println("[OTA] Start"); });
    ArduinoOTA.onEnd([]()    { Serial.println("[OTA] Done"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error %u\n", e); });
    ArduinoOTA.begin();

    registerCommonEndpoints();

    s_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["running"] = Ble.isRunning();
        doc["ble_ok"]  = Ble.lastSuccess();
        doc["ssid"]    = WiFi.SSID();
        doc["ip"]      = WiFi.localIP().toString();
        doc["rssi"]    = WiFi.RSSI();
        String output;
        serializeJson(doc, output);
        req->send(200, "application/json", output);
    });

    s_server.on("/api/boost", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (Ble.isRunning()) {
            req->send(409, "application/json", "{\"error\":\"already running\"}");
            return;
        }
        Ble.trigger();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/reprovision", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        factoryReset();
    });

    s_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    s_server.begin();

    Mqtt.setCommandCallback([]() { Ble.trigger(); });
    Mqtt.setup(s_deviceId.c_str());
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
    }

    s_deviceId = getDeviceId();
    Serial.printf("\n[Main] WhisperBridge  id=%s\n", s_deviceId.c_str());

#ifdef STATUS_LED_PIN
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
#endif
#ifdef RESET_BUTTON_PIN
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
#endif

    Ble.setup();

    if (connectWifi()) {
        startStation();
    } else {
        startAP();
    }
}

void loop() {
    if (s_pendingRestart) { delay(500); ESP.restart(); }

#ifdef RESET_BUTTON_PIN
    static unsigned long s_btnPressedAt = 0;
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (s_btnPressedAt == 0) s_btnPressedAt = millis();
        else if (millis() - s_btnPressedAt >= 5000) {
            Serial.println("[Reset] Button held 5 s — clearing credentials");
            factoryReset();
            s_btnPressedAt = 0;
        }
    } else {
        s_btnPressedAt = 0;
    }
#endif

    if (s_apMode) {
        s_dns.processNextRequest();
        pollScan();
        return;
    }

    ArduinoOTA.handle();
    Mqtt.loop();

    // Once the BLE task finishes, publish the OFF state
    static bool s_wasRunning = false;
    const bool  running      = Ble.isRunning();
    if (s_wasRunning && !running) {
        Mqtt.publishState(false);
    }
    s_wasRunning = running;

#ifdef STATUS_LED_PIN
    digitalWrite(STATUS_LED_PIN, running ? HIGH : LOW);
#endif
}
