# iNFINITEAi2025

> **ESP32 commander and integrations program** — Cloud-connected, AI-controlled,  
> Flipper Zero + Mobile integrated firmware with an on-board self-healing & self-improving engine.

---

## ✨ Feature Overview

| Module | Description |
|---|---|
| **CloudManager** | WiFi (STA + fallback AP), TLS MQTT, HTTPS REST, OTA firmware updates |
| **AiController** | Calls a cloud LLM (OpenAI-compatible) for decisions, diagnosis, and evaluation |
| **FlipperBridge** | UART + BLE communication with a Flipper Zero (NFC, RF, GPIO, IR, custom commands) |
| **MobileApi** | Async HTTP REST server + BLE GATT server for mobile app and PC control |
| **FirmwareInnovator** | Self-healing loop: AI diagnose → Sandbox test → AI evaluate → iterate until PASS |
| **Sandbox** | Heap-guarded, exception-safe execution environment for testing innovations |
| **ProcedureStore** | Persists validated innovations to SPIFFS with auto-generated **funny names** |

---

## 🏗 Architecture

```
Mobile / PC / Cloud Server
        │  (HTTP REST · BLE · MQTT)
        ▼
┌───────────────────────────────────────────────────────────┐
│                    ESP32 iNFINITEAi2025                   │
│                                                           │
│  CloudManager  ◄──► MQTT broker / cloud REST services    │
│  AiController  ◄──► LLM API (OpenAI-compatible)          │
│  MobileApi     ◄──► Mobile app / PC (HTTP + BLE GATT)    │
│  FlipperBridge ◄──► Flipper Zero (UART on GPIO 16/17)    │
│                                                           │
│  FirmwareInnovator (self-improvement loop)                │
│    ├── Sandbox         (safe test execution)              │
│    └── ProcedureStore  (funny-named innovation archive)   │
└───────────────────────────────────────────────────────────┘
        │  (UART · BLE)
        ▼
   Flipper Zero  ──► NFC / RF / GPIO / IR physical actions
```

---

## 🤖 Firmware Innovator — Self-Healing Loop

The on-board innovator continuously improves the firmware without human intervention:

```
Error / task arrives (via MQTT, REST, or watchdog)
        │
        ▼
  [1] AI diagnoses error → proposes solution code
        │
        ▼
  [2] Sandbox executes solution (heap-guarded, exception-safe)
        │
        ▼
  [3] AI evaluates sandbox output
        │
    PASS? ──YES──► Save procedure with funny name (e.g. "SneezyCrumpetV7")
        │                  │
        NO                 └──► Publish to MQTT + notify mobile via BLE
        │
        └──► Feed evaluation back as new error context → repeat (max 50 iter)
```

Saved procedures are stored in SPIFFS at `/proc/<FunnyName>.json` and indexed at `/proc/index.json`.

---

## 🎮 Control Interfaces

### Mobile / PC — REST API

All endpoints require `Authorization: Bearer <API_AUTH_TOKEN>`.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/status` | Device health, connectivity, Flipper status |
| `POST` | `/command` | Send a command JSON (see below) |
| `POST` | `/ai/query` | Direct AI query with custom context |
| `GET` | `/procedures` | List saved innovation procedures |
| `POST` | `/innovate` | Trigger an innovation cycle |

### MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `infiniteai/cmd` | ← subscribe | Commands from cloud/server |
| `infiniteai/status` | → publish | Device status and ping replies |
| `infiniteai/ai` | → publish | AI decision results |
| `infiniteai/innovator` | → publish | Innovation cycle results |

### Command JSON Format

```json
{ "action": "ping" }
{ "action": "innovate", "description": "WiFi reconnect is slow" }
{ "action": "flipper",  "cmd": 16, "payload": { "uid": "DEADBEEF" } }
{ "action": "ai_query", "context": "How do I reduce heap fragmentation?" }
{ "action": "procedures" }
{ "action": "reboot" }
```

### BLE GATT

- **Service UUID**: `12345678-1234-1234-1234-1234567890AB`
- **Command characteristic** (write): `...90AC` — same JSON format as REST `/command`
- **Response characteristic** (notify): `...90AD` — AI results and status updates

### Flipper Zero (UART)

Wire Flipper TX → ESP32 GPIO 16, Flipper RX → ESP32 GPIO 17.  
Packets are newline-delimited JSON: `{"cmd":<uint8>,"payload":<json>}`.

---

## ⚙️ Hardware Setup

| Signal | ESP32 Pin | Connected To |
|---|---|---|
| Flipper RX data | GPIO 16 | Flipper Zero TX |
| Flipper TX data | GPIO 17 | Flipper Zero RX |
| GND | GND | Flipper Zero GND |

---

## 🚀 Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 DevKit board
- Flipper Zero (optional, for physical RF/NFC actions)

### 1. Configure

Edit `src/config.h` and set:
- `WIFI_SSID` / `WIFI_PASSWORD`
- `MQTT_BROKER` / `MQTT_USER` / `MQTT_PASSWORD`
- `AI_API_URL` / `AI_API_KEY`
- `API_AUTH_TOKEN`

### 2. Build and Flash

```bash
# Serial upload
pio run -e esp32dev_serial --target upload

# OTA upload (device must already be running and on the same network)
pio run -e esp32dev --target upload
```

### 3. Monitor

```bash
pio device monitor --baud 115200
```

### 4. Run Native Unit Tests

Download [Unity](https://github.com/ThrowTheSwitch/Unity) (`unity.h`, `unity_internals.h`, `unity.c`) into a local directory, then:

```bash
UNITY=/path/to/unity/src   # e.g. ~/.pio/packages/framework-unity/src
REPO=/path/to/iNFINITEAi2025.

g++ -DNATIVE_TEST -std=c++17 -I$UNITY -I$REPO/src \
    $REPO/test/unit/test_procedure_store.cpp $UNITY/unity.c \
    -o /tmp/test_proc && /tmp/test_proc

g++ -DNATIVE_TEST -std=c++17 -I$UNITY -I$REPO/src \
    $REPO/test/unit/test_sandbox.cpp $UNITY/unity.c \
    -o /tmp/test_sb && /tmp/test_sb

g++ -DNATIVE_TEST -std=c++17 -I$UNITY -I$REPO/src \
    $REPO/test/unit/test_flipper_bridge.cpp $UNITY/unity.c \
    -o /tmp/test_flip && /tmp/test_flip
```

Expected output: `17 Tests 0 Failures 0 Ignored — OK`

---

## 📁 Project Structure

```
iNFINITEAi2025/
├── platformio.ini                  # PlatformIO build config
├── src/
│   ├── main.cpp                    # ESP32 setup() / loop()
│   ├── config.h                    # All configurable constants
│   ├── cloud/
│   │   ├── cloud_manager.h/.cpp    # WiFi + MQTT + REST + OTA
│   ├── ai/
│   │   ├── ai_controller.h/.cpp    # LLM query, diagnose, evaluate
│   ├── flipper/
│   │   ├── flipper_bridge.h/.cpp   # Flipper Zero UART bridge
│   ├── mobile/
│   │   ├── mobile_api.h/.cpp       # HTTP REST + BLE GATT server
│   ├── innovator/
│   │   ├── firmware_innovator.h/.cpp  # Self-healing innovation loop
│   │   ├── sandbox.h/.cpp             # Heap-safe test execution
│   └── procedures/
│       ├── procedure_store.h/.cpp  # SPIFFS procedure persistence
└── test/
    └── unit/
        ├── test_procedure_store.cpp
        ├── test_sandbox.cpp
        └── test_flipper_bridge.cpp
```

---

## 🔐 Security Notes

- REST API uses Bearer token authentication on every endpoint.
- Credentials (`WIFI_PASSWORD`, `MQTT_PASSWORD`, `AI_API_KEY`, `API_AUTH_TOKEN`) must be changed from defaults before deployment.
- MQTT uses TLS (port 8883) — configure your broker certificate accordingly.
- The Sandbox caps heap usage to prevent innovation cycles from crashing the device.

---

## 📜 License

MIT
