'use strict';

const express = require('express');
const path    = require('path');

const PORT        = 3000;
const STATIC_ROOT = path.join(__dirname, '../../WhisperServer/data');

const app = express();
app.use(express.json());

// ── State ─────────────────────────────────────────────────────────────────────

const bootMs = Date.now();
function uptime() { return Date.now() - bootMs; }

// Mirror EspLogger.h timestamp format: uptime string before NTP, ISO UTC after.
function fmtUptime(ms) {
    if (ms < 60000)   return (ms / 1000).toFixed(1) + 's';
    if (ms < 3600000) return Math.floor(ms / 60000) + 'm' + String(Math.floor((ms % 60000) / 1000)).padStart(2, '0') + 's';
    return Math.floor(ms / 3600000) + 'h' + String(Math.floor((ms % 3600000) / 60000)).padStart(2, '0') + 'm' + String(Math.floor((ms % 60000) / 1000)).padStart(2, '0') + 's';
}
function isoNow() { return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'); }

function pushLog(msg, useIso = false) {
    const t = useIso ? isoNow() : fmtUptime(uptime());
    mockLogs.push({ t, msg });
    if (mockLogs.length > 40) mockLogs.shift();
}

let state = { running: false, ble_ok: true };
let mockLogs = [];

// Simulate startup sequence: early entries use uptime, later entries use ISO (post-NTP)
setTimeout(() => pushLog('[Device] ID: whisperbridge-mock'),          50);
setTimeout(() => pushLog('[BLE] Task started'),                       300);
setTimeout(() => pushLog('[WiFi] Connected to HomeNetwork', true),    1100);
setTimeout(() => pushLog('[mDNS] whisperbridge.local', true),         1200);
setTimeout(() => pushLog('[MQTT] Connected', true),                   1400);

// Simulate a BLE sequence: running for 3 s, then done
function simulateBoost() {
    state.running = true;
    pushLog('Boost triggered via API', true);
    setTimeout(() => {
        state.running = false;
        state.ble_ok  = true;
        pushLog('BLE boost complete: OK', true);
        console.log('[BLE] Simulated boost complete');
    }, 3000);
}

// ── API ───────────────────────────────────────────────────────────────────────

app.get('/api/status', (_req, res) => {
    res.json({
        running: state.running,
        ble_ok:  state.ble_ok,
        ssid:    'HomeNetwork',
        ip:      '192.168.1.99',
        rssi:    -55,
    });
});

app.post('/api/boost', (_req, res) => {
    if (state.running) {
        return res.status(409).json({ error: 'already running' });
    }
    simulateBoost();
    res.json({ ok: true });
});

app.get('/api/deviceinfo', (_req, res) => {
    res.json({ id: 'whisperbridge-mock', url: 'http://whisperbridge.local' });
});

app.get('/api/networkdata', (_req, res) => {
    res.json({
        scanning: false,
        networks: [
            { ssid: 'HomeNetwork',   rssi: -45, secure: true  },
            { ssid: 'NeighbourWiFi', rssi: -72, secure: true  },
            { ssid: 'OpenHotspot',   rssi: -80, secure: false },
        ],
    });
});

app.get('/api/logs', (_req, res) => res.json(mockLogs));

let mockSettings = {
    mqtt_host: '192.168.1.x',
    mqtt_port: 1883,
    mqtt_user: '',
    mqtt_pass: '',
    fan_mac: '58:2B:DB:34:D3:AE',
    ota_pass_set: false,
};

app.get('/api/settings', (_req, res) => res.json(mockSettings));

app.post('/api/settings', (req, res) => {
    const b = req.body;
    if (b.mqtt_host !== undefined) mockSettings.mqtt_host = b.mqtt_host;
    if (b.mqtt_port !== undefined) mockSettings.mqtt_port = b.mqtt_port;
    if (b.mqtt_user !== undefined) mockSettings.mqtt_user = b.mqtt_user;
    if (b.mqtt_pass !== undefined) mockSettings.mqtt_pass = b.mqtt_pass;
    if (b.fan_mac   !== undefined) mockSettings.fan_mac = b.fan_mac;
    if (b.ota_pass  !== undefined) mockSettings.ota_pass_set = b.ota_pass.length > 0;
    const restart = b.mqtt_host !== undefined || b.mqtt_port !== undefined ||
                    b.mqtt_user !== undefined || b.mqtt_pass !== undefined ||
                    b.ota_pass  !== undefined;
    res.json({ ok: true, restart });
});

app.post('/api/networkset', (req, res) => {
    console.log(`[WiFi] Credentials saved — SSID: ${req.body.ssid}`);
    res.json({ ok: true });
});

// ── Static files (WhisperServer/data/) ───────────────────────────────────────

app.use(express.static(STATIC_ROOT));
app.get('/', (_req, res) => res.sendFile(path.join(STATIC_ROOT, 'index.html')));

// ── Start ─────────────────────────────────────────────────────────────────────

app.listen(PORT, () => {
    console.log(`WhisperBridge test server → http://localhost:${PORT}`);
    console.log(`Serving static files from  → ${STATIC_ROOT}`);
});
