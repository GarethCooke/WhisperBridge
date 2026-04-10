'use strict';

const express = require('express');
const path    = require('path');

const PORT        = 3000;
const STATIC_ROOT = path.join(__dirname, '../../WhisperServer/data');

const app = express();
app.use(express.json());

// ── State ─────────────────────────────────────────────────────────────────────

let state = { running: false, ble_ok: true };

// Simulate a BLE sequence: running for 3 s, then done
function simulateBoost() {
    state.running = true;
    setTimeout(() => {
        state.running = false;
        state.ble_ok  = true;
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
