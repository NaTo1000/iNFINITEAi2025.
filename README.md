# iNFINITEAi2025

ESP32 control firmware that connects mobile/PC clients, MQTT/cloud services, and a Flipper Zero bridge.

## What this repository now implements

- ESP32 firmware entrypoint with Wi-Fi, MQTT, HTTPS, OTA, BLE, REST, UART, and procedure persistence modules.
- Cloud AI integration for **structured JSON advice**, not local model inference.
- An **evidence-driven ingestion and review loop** that normalizes evidence, applies QC thresholds, compares reviewer outputs, and saves audit-ready reports.
- A **safe procedure-generation loop** that validates allowlisted actions and saves approved procedures only when the evidence-backed review passes QC.
- Native unit tests plus GitHub Actions CI that runs:
  - `pio run -e esp32dev`
  - `pio run -e esp32dev_serial`
  - `pio test -e native`

## Important safety model

This project does **not** compile or execute arbitrary AI-generated C++ on-device.

Instead, the innovator requests strict JSON containing:

- a structured review report with source metadata, evidence, analysis, reviewer outputs, QC scores, and bounded recovery actions
- a structured procedure containing only allowlisted actions:

- `log`
- `publish_status`
- `wait_ms`
- `request_review`

A procedure must also include explicit validation criteria before it can be persisted, and the review report must meet explicit QC thresholds. This means the current firmware supports **bounded evidence review, safe procedure generation, and archival**, not autonomous firmware self-modification or open-ended investigative conclusions.

## Repository layout

```text
src/
  ai/           Structured AI request/response handling
  cloud/        Wi-Fi, MQTT, HTTPS, OTA
  common/       Native test helpers
  flipper/      UART bridge and packet validation
  innovator/    Sandbox + bounded innovation state machine
  mobile/       REST + BLE command surface
  procedures/   Persistent validated procedure storage
  main.cpp      ESP32 setup()/loop()
```

## Configuration and secrets

Tracked source only contains defaults and limits. Real credentials must go in an ignored file:

1. Copy `/home/runner/work/iNFINITEAi2025./iNFINITEAi2025./src/config_secrets.example.h`
2. Save it as `/home/runner/work/iNFINITEAi2025./iNFINITEAi2025./src/config_secrets.h`
3. Fill in real values for Wi-Fi, MQTT, AI, API, TLS CA, and OTA auth.

`src/config_secrets.h` is gitignored.

### Required secure configuration

- `TLS_ROOT_CA` must be set for HTTPS and TLS MQTT.
- `API_AUTH_TOKEN` must be a non-default bearer token.
- `MQTT_COMMAND_TOKEN` must be set for authenticated MQTT control messages.
- `OTA_PASSWORD_HASH` must be set before OTA is enabled.

If credentials are missing, the firmware falls back to provisioning/AP behavior or disables the affected feature instead of using insecure defaults.

## Build and test

### Local PlatformIO commands

```bash
pio run -e esp32dev
pio run -e esp32dev_serial
pio test -e native
```

### Native environment notes

The native test environment uses repository-relative headers through `-Isrc` and `test_build_src = yes`, so tests include headers like:

```cpp
#include "ai/ai_controller.h"
```

## REST API

All HTTP endpoints require:

```text
Authorization: ******
```

### Endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/status` | Connectivity and device status |
| `POST` | `/command` | Queue a validated control command |
| `POST` | `/ai/query` | Send a direct structured AI request |
| `GET` | `/procedures` | List saved validated procedures with audit sections |
| `POST` | `/innovate` | Queue a new innovation task |
| `GET` | `/innovate/log` | Retrieve the bounded innovation log plus live review stage/report data |

### Request constraints

- JSON body required for POST routes
- request body limit: `API_MAX_BODY_BYTES`
- fragmented request bodies are reassembled using `index`/`total`
- malformed or oversized bodies return structured errors
- requests are rate limited

### Dangerous commands

Dangerous commands such as `reboot` and physical Flipper actions require:

- prior API authentication
- `confirm: true`
- for MQTT, an `auth` token and a fresh `nonce`

## BLE

BLE mirrors the command surface for nearby controllers.

- pairing/authentication is enabled where supported by NimBLE
- BLE writes must include an `auth` field matching `API_AUTH_TOKEN`
- BLE is intended for trusted local clients; platform-specific bonding limitations should still be validated on hardware

## MQTT and cloud behavior

- MQTT payloads are size-limited.
- Malformed MQTT command payloads are rejected.
- HTTPS requests use explicit timeouts, retries, response-size limits, and HTTP status handling.
- Sensitive values such as bearer tokens and API keys are not logged.

For authenticated MQTT control, use JSON shaped like:

```json
{
  "action": "innovate",
  "description": "WiFi reconnect is slow",
  "auth": "<MQTT_COMMAND_TOKEN>",
  "nonce": "unique-command-id",
  "ts": 1735689600000
}
```

## Flipper Zero bridge

Packets are newline-delimited JSON:

```json
{
  "cmd": 16,
  "payload": {
    "uid": "DEADBEEF"
  }
}
```

Implemented protections:

- ping/ACK connectivity check
- maximum packet size
- malformed frame rejection
- nested JSON payload encoding
- validation for GPIO pin, RF frequency, and hex payloads

Physical NFC/RF/GPIO/IR actions can have legal or safety consequences. Only use them with explicit authorization and local confirmation.

## Procedure persistence

Validated evidence-review reports are stored atomically and indexed under the procedure store.

Implemented safeguards:

- atomic temp-file write + rename
- index rebuild by scanning stored procedures
- corruption fallback
- procedure size/count limits
- free-space checks
- sanitized unique filenames

Stored report summaries expose separate `evidence`, `analysis`, reviewer/QC, and `recommendedRecoveryActions` sections for auditability.

## OTA

OTA is disabled unless `OTA_PASSWORD_HASH` is configured.

Current limitations:

- authenticated OTA is supported
- secure rollback/signature infrastructure is **not** implemented in this repository
- deploy only in environments where your OTA transport and release pipeline are already trusted

## Hardware and integration testing guidance

Native tests cover parser, persistence, Flipper framing, mobile body buffering, and innovation state transitions.

Hardware-specific behavior still needs on-device verification for:

- Wi-Fi association and provisioning AP
- TLS MQTT connectivity with your broker CA
- HTTPS AI provider compatibility
- BLE pairing/bonding behavior on your target client devices
- OTA updates on your selected partition scheme
- UART connectivity to a real Flipper Zero

Where hardware is unavailable, the modules expose native-safe code paths or mocks so command handling, persistence, and AI/procedure logic can still be exercised in CI.
