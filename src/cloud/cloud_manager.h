#pragma once
// =============================================================================
// cloud_manager.h — WiFi, MQTT, and REST cloud connectivity
// =============================================================================
#ifndef NATIVE_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#endif

#include <functional>
#include <string>
#include "../config.h"

// Callback types
using CloudCommandCallback = std::function<void(const std::string& topic, const std::string& payload)>;

class CloudManager {
public:
    CloudManager();

    // Lifecycle
    bool begin();
    void loop();

    // Connectivity
    bool isWiFiConnected() const;
    bool isMqttConnected() const;

    // Publishing
    bool publishStatus(const std::string& jsonPayload);
    bool publishAiResult(const std::string& jsonPayload);
    bool publishInnovatorResult(const std::string& jsonPayload);
    bool publish(const std::string& topic, const std::string& payload);

    // REST helper — POST JSON to AI endpoint
    std::string httpPost(const std::string& url,
                         const std::string& bearerToken,
                         const std::string& jsonBody);

    // Subscribe for commands from cloud/mobile
    void onCommand(CloudCommandCallback cb) { _commandCb = cb; }

    // OTA
    void enableOta();

private:
    void _connectWifi();
    void _connectMqtt();
    void _mqttCallback(const char* topic, const uint8_t* payload, unsigned int len);

    CloudCommandCallback _commandCb;

#ifndef NATIVE_TEST
    WiFiClientSecure  _wifiClient;
    PubSubClient      _mqtt;
    unsigned long     _lastMqttReconnect = 0;
    bool              _otaEnabled        = false;
#endif
};
