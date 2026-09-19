#pragma once
// Copy this file to src/config_secrets.h and fill in real values.
// The real file is ignored by git.

#define WIFI_SSID "replace-me"
#define WIFI_PASSWORD "replace-me"
#define MQTT_BROKER "mqtt.example.com"
#define MQTT_PORT 8883
#define MQTT_USER "replace-me"
#define MQTT_PASSWORD "replace-me"
#define MQTT_COMMAND_TOKEN "replace-me-with-long-random-token"
#define TLS_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
#define AI_API_URL "https://api.example.com/v1/chat/completions"
#define AI_API_KEY "replace-me"
#define AI_MODEL "gpt-4o-mini"
#define API_AUTH_TOKEN "replace-me-with-long-random-token"
#define OTA_PASSWORD_HASH "md5-or-ota-password-hash"
#define BLE_PAIRING_PASSKEY 123456
