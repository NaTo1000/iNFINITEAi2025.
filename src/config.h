#pragma once
// =============================================================================
// config.h — iNFINITEAi2025 system-wide configuration defaults
//
// Secrets and deployment-specific values must be supplied through an ignored
// src/config_secrets.h file or PlatformIO build flags. See
// src/config_secrets.example.h.
// =============================================================================
#include <cstddef>
#include <cstdint>

#if defined(__has_include)
#  if __has_include("config_secrets.h")
#    include "config_secrets.h"
#  endif
#endif

// ---------------------------------------------------------------------------
// Device identity
// ---------------------------------------------------------------------------
#define DEVICE_NAME              "iNFINITEAi2025"
#define DEVICE_VERSION           "1.1.0"
#define OTA_HOSTNAME             "infiniteai.local"

// ---------------------------------------------------------------------------
// WiFi (primary + fallback provisioning AP)
// ---------------------------------------------------------------------------
#ifndef WIFI_SSID
#define WIFI_SSID                ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD            ""
#endif
#ifndef WIFI_AP_SSID_PREFIX
#define WIFI_AP_SSID_PREFIX      "iNFINITEAi-"
#endif
#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD         ""
#endif
#define WIFI_CONNECT_TIMEOUT_MS  15000UL

// ---------------------------------------------------------------------------
// Cloud / MQTT broker
// ---------------------------------------------------------------------------
#ifndef MQTT_BROKER
#define MQTT_BROKER              ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT                8883
#endif
#ifndef MQTT_USER
#define MQTT_USER                ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD            ""
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID           DEVICE_NAME
#endif
#ifndef MQTT_TOPIC_CMD
#define MQTT_TOPIC_CMD           "infiniteai/cmd"
#endif
#ifndef MQTT_TOPIC_STATUS
#define MQTT_TOPIC_STATUS        "infiniteai/status"
#endif
#ifndef MQTT_TOPIC_AI
#define MQTT_TOPIC_AI            "infiniteai/ai"
#endif
#ifndef MQTT_TOPIC_INNO
#define MQTT_TOPIC_INNO          "infiniteai/innovator"
#endif
#ifndef MQTT_COMMAND_TOKEN
#define MQTT_COMMAND_TOKEN       ""
#endif
#ifndef TLS_ROOT_CA
#define TLS_ROOT_CA              ""
#endif
#define MQTT_RECONNECT_DELAY_MS  5000UL
#define MQTT_MAX_PAYLOAD_BYTES   512U
#define COMMAND_MAX_NONCE_CACHE  16U
#define COMMAND_MAX_SKEW_MS      300000UL

// ---------------------------------------------------------------------------
// AI / LLM cloud endpoint
// ---------------------------------------------------------------------------
#ifndef AI_API_URL
#define AI_API_URL               ""
#endif
#ifndef AI_API_KEY
#define AI_API_KEY               ""
#endif
#ifndef AI_MODEL
#define AI_MODEL                 "gpt-4o-mini"
#endif
#define AI_MAX_TOKENS            512
#define AI_TEMP                  0.2f
#define AI_MAX_CONTENT_BYTES     1536U
#define AI_MAX_STEPS             8U
#define AI_MAX_VALIDATION_RULES  6U

// ---------------------------------------------------------------------------
// HTTP / HTTPS request handling
// ---------------------------------------------------------------------------
#define HTTP_CONNECT_TIMEOUT_MS  5000UL
#define HTTP_REQUEST_TIMEOUT_MS  10000UL
#define HTTP_RETRY_COUNT         3U
#define HTTP_MAX_RESPONSE_BYTES  4096U

// ---------------------------------------------------------------------------
// Mobile / PC REST API
// ---------------------------------------------------------------------------
#ifndef API_PORT
#define API_PORT                 80
#endif
#ifndef API_AUTH_TOKEN
#define API_AUTH_TOKEN           ""
#endif
#define API_MAX_BODY_BYTES       1024U
#define API_RATE_LIMIT_WINDOW_MS 1000UL
#define API_RATE_LIMIT_MAX_CALLS 5U

// ---------------------------------------------------------------------------
// Flipper Zero UART bridge
// ---------------------------------------------------------------------------
#define FLIPPER_UART_RX          16
#define FLIPPER_UART_TX          17
#define FLIPPER_BAUD             115200
#define FLIPPER_TIMEOUT_MS       2000UL
#define FLIPPER_MAX_PACKET_BYTES 512U
#define FLIPPER_MIN_RF_HZ        300000000UL
#define FLIPPER_MAX_RF_HZ        928000000UL
#define FLIPPER_MAX_HEX_BYTES    256U

// ---------------------------------------------------------------------------
// BLE (used by both Flipper Zero BLE and mobile app)
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME          DEVICE_NAME
#define BLE_SERVICE_UUID         "12345678-1234-1234-1234-1234567890AB"
#define BLE_CMD_CHAR_UUID        "12345678-1234-1234-1234-1234567890AC"
#define BLE_RSP_CHAR_UUID        "12345678-1234-1234-1234-1234567890AD"
#ifndef BLE_PAIRING_PASSKEY
#define BLE_PAIRING_PASSKEY      123456
#endif

// ---------------------------------------------------------------------------
// Firmware Innovator (safe procedure generation, not firmware self-patching)
// ---------------------------------------------------------------------------
#define INNOVATOR_SANDBOX_HEAP_LIMIT   (40U * 1024U)
#define INNOVATOR_MAX_ITERATIONS       10U
#define INNOVATOR_MIN_SOLUTION_LENGTH  16U
#define INNOVATOR_CYCLE_DELAY_MS       250UL
#define INNOVATOR_MAX_LOG_BYTES        4096U
#define INNOVATOR_MAX_STEP_DELAY_MS    5000U
#define INNOVATOR_QC_PASS_SCORE        90U
#define INNOVATOR_QC_MAX_RISK_SCORE    20U
#define INNOVATOR_MIN_EVIDENCE_ITEMS   1U
#define INNOVATOR_MIN_REVIEWERS        2U
#define PROCEDURES_NAMESPACE           "procedures"

// ---------------------------------------------------------------------------
// Procedure persistence
// ---------------------------------------------------------------------------
#define PROCEDURE_MAX_COUNT            32U
#define PROCEDURE_MAX_DESCRIPTION_BYTES 240U
#define PROCEDURE_MAX_CODE_BYTES       6144U
#define PROCEDURE_MIN_FREE_BYTES       8192U

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
#ifndef OTA_PASSWORD_HASH
#define OTA_PASSWORD_HASH              ""
#endif

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
