# WhisperBridge Setup Guide

## Overview

WhisperBridge is an ESP32-based BLE-to-MQTT bridge that controls a Vent-Axia Svara bathroom fan via Alexa and Home Assistant. All control is local except the Alexa integration which uses AWS Lambda.

## Architecture

**Control flow:** Alexa → AWS Lambda → Home Assistant API → Mosquitto MQTT → ESP32 → BLE → Vent-Axia Svara fan

**Hardware:**

- ESP32 DevKit V1 (PlatformIO board target: `esp32dev`)
- Raspberry Pi 5 running Home Assistant OS

---

## BLE Protocol (Verified Working)

Two-step sequence to control the fan:

### Step 1 — Authentication

- Write PIN `e3146205` to characteristic `4cad343a-209a-40b7-b911-4d9b3df569b2`
- Service: `e6834e4b-7b3a-48e6-91e4-f1d005f564d3`

### Step 2 — Boost Command

- Write `01 60 09 84 03` to characteristic `118c949c-28c8-4139-b0b3-36657fd055a9`
- Service: `c119e858-0531-4681-9674-5a11f0e53bb4`
- Bytes: `01` = on, `6009` = 2310 RPM, `8403` = 900 seconds (15 mins)

### ESP32 Firmware Libraries

- NimBLE-Arduino (BLE)
- PubSubClient (MQTT)
- WiFi

---

## Raspberry Pi 5 — Home Assistant Setup

### Add-ons Installed

- Mosquitto MQTT broker (official HA add-on)
- Cloudflared (brenner-tobias/ha-addons repository)
- Studio Code Server
- Terminal & SSH

### Mosquitto Broker Configuration

Configured via the HA add-on UI (Settings → Add-ons → Mosquitto broker → Configuration):

- Login created: username `whisperbridge` with password (stored in add-on config)
- Port 1883 (standard MQTT) — used by the ESP32 on the local network
- Port 8883 (MQTT with SSL) also available

Test MQTT connectivity from the Pi:

```bash
mosquitto_sub -t whisperbridge/# -v -u whisperbridge -p 1883 -P YOUR_PASSWORD
```

### configuration.yaml

```yaml
mqtt:
  switch:
    - name: "Whisperbridge Boost"
      unique_id: whisperbridge_boost
      command_topic: "whisperbridge/boost"
      state_topic: "whisperbridge/boost"
      payload_on: "ON"
      payload_off: "OFF"
      retain: false

http:
  use_x_forwarded_for: true
  trusted_proxies:
    - 172.30.33.0/24
    - 127.0.0.1

alexa:
  smart_home:
    filter:
      include_entities:
        - switch.whisperbridge_boost
```

The `alexa:` block tells HA which entities to expose when Alexa sends a discovery request.

---

## External Access — Cloudflare Tunnel

### Domain Setup

- Domain: `iguanacarver.com` (moved from AWS Route 53 to Cloudflare DNS)
- Cloudflare free plan
- Nameservers: `grant.ns.cloudflare.com` and `monroe.ns.cloudflare.com`

### Cloudflare Zero Trust

- Team name: `iguanacarver`
- Tunnel name: `iguanacarver`
- Tunnel ID: `04ba81cb-b69e-43c7-be77-e14a2d4a94fc`
- Route: `ha.iguanacarver.com` → `http://192.168.68.78:8123`

### Cloudflared Add-on Config

- External Home Assistant Hostname: `ha.iguanacarver.com`
- Cloudflare Tunnel Token: stored in add-on config (not recorded here)

### Cloudflare WAF Rule — Skip Browser Integrity Check for API

Cloudflare's Browser Integrity Check blocks non-browser HTTP requests (such as those from AWS Lambda) by default. A custom WAF rule is required to allow Lambda to reach the HA API.

**Security → Security rules → Custom rules:**

- Rule name: `Skip BIC for API`
- When: URI Path starts with `/api/`
- Action: Skip → Browser Integrity Check
- Expression: `(starts_with(http.request.uri.path, "/api/"))`

Without this rule, Lambda calls to `https://ha.iguanacarver.com/api/services/mqtt/publish` return HTTP 403.

**Result:** HA accessible at `https://ha.iguanacarver.com` ✅

---

## Alexa Integration — AWS Lambda

### Account Setup

The Alexa Developer Console, AWS account, and Echo devices must all be on the **same Amazon identity**. The Alexa Developer Console validates the Lambda ARN against the owning AWS account, and unpublished dev skills are only visible to the Amazon account that owns them.

- Amazon account used: `gareth.w.cooke@googlemail.com` (same account as Echo devices)
- AWS account: `gareth_cooke@hotmail.com` (separate identity — works because Alexa Developer Console validates via the Skill ID in the Lambda resource policy, not email matching)

**Key lesson learned:** The Alexa Developer Console error "Please make sure Alexa Smart Home is selected for the event source type" is caused by a mismatch between the **Skill ID** in the Lambda's Alexa Smart Home trigger and the Skill ID of the skill in the Developer Console. When a new skill is created, it gets a new Skill ID — the Lambda trigger must be updated to match.

### Alexa Skill

- Name: Home Assistant
- Type: Smart Home (must be Smart Home, not Custom — cannot be changed after creation)
- Payload version: v3
- Locale: English (UK/IE)
- Skill ID: `amzn1.ask.skill.bd575423-fdc5-4f0e-8177-ec414a9d1acc`

### AWS Lambda Function

- Name: `homeAssistant`
- Region: `eu-west-1` (Ireland)
- Runtime: Python 3.12
- Architecture: arm64
- Memory: 128 MB
- Timeout: 3 seconds
- ARN: `arn:aws:lambda:eu-west-1:887401768002:function:homeAssistant`

### Lambda Trigger

- Type: Alexa Smart Home (`ash`)
- Event source token: `amzn1.ask.skill.bd575423-fdc5-4f0e-8177-ec414a9d1acc`
- Service principal: `alexa-connectedhome.amazon.com`

To verify the Lambda resource policy is correct:

```bash
aws lambda get-policy --function-name homeAssistant --region eu-west-1 --output json
```

The policy must show Principal `alexa-connectedhome.amazon.com` (not `alexa-appkit.amazon.com` which is for Custom skills) and the correct Skill ID in the EventSourceToken condition.

### Alexa Developer Console — Smart Home Endpoint

- Default endpoint: `arn:aws:lambda:eu-west-1:887401768002:function:homeAssistant`
- Europe, India (checked): `arn:aws:lambda:eu-west-1:887401768002:function:homeAssistant`

### Account Linking

Configured in the Alexa Developer Console under Account Linking. HA has a built-in OAuth2 provider.

- Authorization URI: `https://ha.iguanacarver.com/auth/authorize`
- Access Token URI: `https://ha.iguanacarver.com/auth/token`
- Client ID: `https://layla.amazon.com/` (for EU accounts — note trailing slash)
- Client Secret: any dummy string (HA doesn't validate it but the form requires a value)
- Authentication Scheme: Credentials in request body
- Scope: `smart_home`

**Note on Client ID:** `https://pitangui.amazon.com/` is for North America. For EU accounts the redirect goes through `layla.amazon.com`, and using the wrong domain causes an "invalid redirect URI" error during account linking in the Alexa app.

Alexa Redirect URLs (auto-generated, for reference):

- `https://pitangui.amazon.com/api/skill/link/M25UJNAV5WPVMK`
- `https://layla.amazon.com/api/skill/link/M25UJNAV5WPVMK`
- `https://alexa.amazon.co.jp/api/skill/link/M25UJNAV5WPVMK`

### Lambda Code

The Lambda handles three directive types directly (without proxying to HA's Alexa integration). It uses a HA long-lived access token to call the HA REST API for MQTT publishing.

```python
import json
import logging
import urllib.request

logger = logging.getLogger()
logger.setLevel(logging.INFO)

HA_URL = "https://ha.iguanacarver.com"
HA_TOKEN = "<long-lived-access-token>"

def lambda_handler(event, context):
    logger.info("Event: %s", json.dumps(event))

    namespace = event.get("directive", {}).get("header", {}).get("namespace", "")
    name = event.get("directive", {}).get("header", {}).get("name", "")

    if namespace == "Alexa.Discovery":
        return handle_discovery()
    elif namespace == "Alexa.PowerController":
        return handle_power(event)
    elif namespace == "Alexa" and name == "ReportState":
        return handle_report_state(event)

def handle_discovery():
    return {
        "event": {
            "header": {
                "namespace": "Alexa.Discovery",
                "name": "Discover.Response",
                "payloadVersion": "3",
                "messageId": "discovery-response"
            },
            "payload": {
                "endpoints": [{
                    "endpointId": "whisperbridge_boost",
                    "friendlyName": "Bathroom Fan",
                    "description": "Vent Axia Fan Boost",
                    "manufacturerName": "WhisperBridge",
                    "capabilities": [
                    {
                        "type": "AlexaInterface",
                        "interface": "Alexa",
                        "version": "3"
                    },
                    {
                        "type": "AlexaInterface",
                        "interface": "Alexa.PowerController",
                        "version": "3",
                        "properties": {
                            "supported": [{"name": "powerState"}],
                            "retrievable": True
                        }
                    }]
                }]
            }
        }
    }

def handle_power(event):
    directive_name = event["directive"]["header"]["name"]
    power_state = "ON" if directive_name == "TurnOn" else "OFF"

    url = f"{HA_URL}/api/services/mqtt/publish"
    data = json.dumps({
        "topic": "whisperbridge/boost",
        "payload": power_state
    }).encode()

    req = urllib.request.Request(
        url,
        data=data,
        headers={
            "Authorization": f"Bearer {HA_TOKEN}",
            "Content-Type": "application/json"
        }
    )
    urllib.request.urlopen(req)

    return {
        "event": {
            "header": {
                "namespace": "Alexa",
                "name": "Response",
                "payloadVersion": "3",
                "messageId": "power-response",
                "correlationToken": event["directive"]["header"]["correlationToken"]
            },
            "endpoint": event["directive"]["endpoint"],
            "payload": {}
        },
        "context": {
            "properties": [{
                "namespace": "Alexa.PowerController",
                "name": "powerState",
                "value": power_state,
                "timeOfSample": "2026-04-19T00:00:00.00Z",
                "uncertaintyInMilliseconds": 0
            }]
        }
    }

def handle_report_state(event):
    return {
        "event": {
            "header": {
                "namespace": "Alexa",
                "name": "StateReport",
                "payloadVersion": "3",
                "messageId": "state-report",
                "correlationToken": event["directive"]["header"]["correlationToken"]
            },
            "endpoint": event["directive"]["endpoint"],
            "payload": {}
        },
        "context": {
            "properties": [{
                "namespace": "Alexa.PowerController",
                "name": "powerState",
                "value": "OFF",
                "timeOfSample": "2026-04-20T00:00:00.00Z",
                "uncertaintyInMilliseconds": 5000
            }]
        }
    }
```

**Notes on the Lambda code:**

- The `Alexa` base interface (with no properties) must be included in every endpoint's capabilities list alongside any specific interfaces like `PowerController`.
- The `handle_report_state` function is required — Alexa polls endpoint state periodically and the Lambda must respond or Alexa will report the device as unresponsive.
- The `correlationToken` from the incoming directive must be included in PowerController and ReportState responses.
- The `friendlyName` ("Bathroom Fan") is what Alexa uses for voice commands: "Alexa, turn on Bathroom Fan".
- The handler file must be named `lambda_function.py` and the handler setting must be `lambda_function.lambda_handler`.
- The HA long-lived access token is used directly rather than the OAuth bearer token from Account Linking. Account Linking is still required by Alexa but the OAuth token is not used for API calls.

### Enabling the Skill

1. In the Alexa app: More → Skills & Games → Your Skills → Dev
2. Find the skill and tap Enable
3. Complete the OAuth flow (redirects to HA login via Cloudflare Tunnel)
4. Say "Alexa, discover devices" — "Bathroom Fan" should appear
5. Test: "Alexa, turn on Bathroom Fan"

---

## Debugging Reference

### CloudWatch Logs

Lambda logs are at: CloudWatch → Log groups → `/aws/lambda/homeAssistant`

Common issues visible in logs:

- **No response logged, function exits in ~20ms:** Code not deployed (hit Deploy button in Lambda console) or handler not matching (check Runtime settings → Handler is `lambda_function.lambda_handler`)
- **HTTP 403 Forbidden from HA:** Cloudflare Browser Integrity Check blocking the request — add the WAF skip rule (see Cloudflare section above)
- **HTTP 401 Unauthorized from HA:** Long-lived access token is invalid or expired — regenerate in HA under Profile → Long-Lived Access Tokens
- **No log entry after Alexa command:** Skill not enabled in the Alexa app, or skill is on a different Amazon account to the Echo devices

### Lambda Test Events

Create test events in the Lambda console (Test tab) to verify handlers without going through Alexa:

**Discovery test:**

```json
{
  "directive": {
    "header": {
      "namespace": "Alexa.Discovery",
      "name": "Discover",
      "payloadVersion": "3",
      "messageId": "test-discovery"
    },
    "payload": {
      "scope": {
        "type": "BearerToken",
        "token": "test"
      }
    }
  }
}
```

**TurnOn test:**

```json
{
  "directive": {
    "header": {
      "namespace": "Alexa.PowerController",
      "name": "TurnOn",
      "payloadVersion": "3",
      "messageId": "test-turnon",
      "correlationToken": "test-token"
    },
    "endpoint": {
      "scope": {
        "type": "BearerToken",
        "token": "test"
      },
      "endpointId": "whisperbridge_boost"
    },
    "payload": {}
  }
}
```

### MQTT Debugging

Monitor all WhisperBridge MQTT traffic from the Pi:

```bash
mosquitto_sub -t whisperbridge/# -v -u whisperbridge -p 1883 -P YOUR_PASSWORD
```

Verify the HA entity state: Developer Tools → States → search for `whisperbridge`.

---

## Current Status

- ✅ HA external access via Cloudflare Tunnel
- ✅ Mosquitto MQTT broker with WhisperBridge switch entity in HA
- ✅ Alexa Smart Home skill created and configured
- ✅ Lambda function deployed with Discovery, PowerController, and ReportState handlers
- ✅ Account Linking (OAuth) configured and working
- ✅ Cloudflare WAF rule to bypass Browser Integrity Check for API calls
- ✅ Alexa device discovery successful — "Bathroom Fan" visible
- ✅ Alexa → Lambda → HA → MQTT chain verified end to end
- 🔧 ESP32 firmware (BLE-to-MQTT bridge) — implementation in Claude Code (next step)

## Still To Do

1. Implement WhisperBridge ESP32 firmware in Claude Code
2. Test full end-to-end: Alexa voice command → fan physically activates
3. Handle boost OFF (fan returns to normal speed after timer or on explicit OFF command)
