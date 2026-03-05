#pragma once
// =============================================================================
// config.h — iNFINITEAi2025 system-wide configuration
// Edit this file to match your hardware wiring and service credentials.
// =============================================================================

// ---------------------------------------------------------------------------
// Device identity
// ---------------------------------------------------------------------------
#define DEVICE_NAME       "iNFINITEAi2025"
#define DEVICE_VERSION    "1.0.0"
#define OTA_HOSTNAME      "infiniteai.local"

// ---------------------------------------------------------------------------
// WiFi (primary + fallback AP)
// ---------------------------------------------------------------------------
#define WIFI_SSID         "YOUR_WIFI_SSID"
#define WIFI_PASSWORD     "YOUR_WIFI_PASSWORD"
#define WIFI_AP_SSID      "iNFINITEAi_Setup"
#define WIFI_AP_PASSWORD  "infiniteai"
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ---------------------------------------------------------------------------
// Cloud / MQTT broker
// ---------------------------------------------------------------------------
#define MQTT_BROKER       "mqtt.your-cloud.io"
#define MQTT_PORT         8883              // TLS port
#define MQTT_USER         "YOUR_MQTT_USER"
#define MQTT_PASSWORD     "YOUR_MQTT_PASS"
#define MQTT_CLIENT_ID    DEVICE_NAME
#define MQTT_TOPIC_CMD    "infiniteai/cmd"
#define MQTT_TOPIC_STATUS "infiniteai/status"
#define MQTT_TOPIC_AI     "infiniteai/ai"
#define MQTT_TOPIC_INNO   "infiniteai/innovator"
#define MQTT_RECONNECT_DELAY_MS  5000

// ---------------------------------------------------------------------------
// AI / LLM cloud endpoint
// ---------------------------------------------------------------------------
#define AI_API_URL        "https://api.your-ai-provider.io/v1/chat"
#define AI_API_KEY        "YOUR_AI_API_KEY"
#define AI_MODEL          "gpt-4o-mini"
#define AI_MAX_TOKENS     512
#define AI_TEMP           "0.7"

// ---------------------------------------------------------------------------
// Mobile / PC REST API (async HTTP server on ESP32)
// ---------------------------------------------------------------------------
#define API_PORT          80
#define API_AUTH_TOKEN    "YOUR_API_TOKEN"   // Bearer token for REST calls

// ---------------------------------------------------------------------------
// Flipper Zero UART bridge
// ---------------------------------------------------------------------------
#define FLIPPER_UART_RX   16   // GPIO16 → Flipper TX
#define FLIPPER_UART_TX   17   // GPIO17 → Flipper RX
#define FLIPPER_BAUD      115200
#define FLIPPER_TIMEOUT_MS 2000

// ---------------------------------------------------------------------------
// BLE (used by both Flipper Zero BLE and mobile app)
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME   DEVICE_NAME
// Service / characteristic UUIDs (generated once, kept stable)
#define BLE_SERVICE_UUID  "12345678-1234-1234-1234-1234567890AB"
#define BLE_CMD_CHAR_UUID "12345678-1234-1234-1234-1234567890AC"  // write
#define BLE_RSP_CHAR_UUID "12345678-1234-1234-1234-1234567890AD"  // notify

// ---------------------------------------------------------------------------
// Firmware Innovator (self-healing + self-improving loop)
// ---------------------------------------------------------------------------
#define INNOVATOR_SANDBOX_HEAP_LIMIT   (40 * 1024)   // 40 KB — leaves ~120 KB for normal operation on most ESP32 boards
#define INNOVATOR_MAX_ITERATIONS       50             // safety cap; typically converges within 5-10 iterations
#define INNOVATOR_MIN_SOLUTION_LENGTH  10             // AI solutions shorter than this are not viable
#define INNOVATOR_CYCLE_DELAY_MS       500
#define PROCEDURES_NAMESPACE           "procedures"   // NVS namespace

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
#define LOG_TAG_MAIN      "MAIN"
#define LOG_TAG_CLOUD     "CLOUD"
#define LOG_TAG_AI        "AI"
#define LOG_TAG_FLIPPER   "FLIPPER"
#define LOG_TAG_MOBILE    "MOBILE"
#define LOG_TAG_INNO      "INNOVATOR"
#define LOG_TAG_SANDBOX   "SANDBOX"
#define LOG_TAG_PROC      "PROCEDURES"
