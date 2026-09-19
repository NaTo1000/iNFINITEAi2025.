#pragma once
// =============================================================================
// cloud_manager.h — WiFi, MQTT, HTTPS, and OTA connectivity
// =============================================================================
#ifndef NATIVE_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../config.h"

struct HttpResponse {
    bool        success    = false;
    int         statusCode = 0;
    uint8_t     attempts   = 0;
    std::string body;
    std::string error;
};

using CloudCommandCallback = std::function<void(const std::string& topic, const std::string& payload)>;

class CloudManager {
public:
    CloudManager();

    bool begin();
    void loop();

    bool isWiFiConnected() const;
    bool isMqttConnected() const;
    bool credentialsProvisioned() const;
    bool hasTrustedTlsConfig() const;

    bool publishStatus(const std::string& jsonPayload);
    bool publishAiResult(const std::string& jsonPayload);
    bool publishInnovatorResult(const std::string& jsonPayload);
    bool publish(const std::string& topic, const std::string& payload);

    HttpResponse httpPost(const std::string& url,
                          const std::string& bearerToken,
                          const std::string& jsonBody);

    void onCommand(CloudCommandCallback cb) { _commandCb = cb; }
    void enableOta();

#ifdef NATIVE_TEST
    void queueMockHttpResponse(const HttpResponse& response);
    void setMockConnectivity(bool wifiConnected, bool mqttConnected);
    const std::vector<std::pair<std::string, std::string>>& publishedMessages() const {
        return _publishedMessages;
    }
    const std::vector<std::string>& httpRequestBodies() const {
        return _httpRequestBodies;
    }
#endif

private:
    bool _isUnset(const char* value) const;
    bool _isRetryableHttpCode(int statusCode) const;
#ifndef NATIVE_TEST
    void _connectWifi();
    void _connectMqtt();
    void _mqttCallback(const char* topic, const uint8_t* payload, unsigned int len);
    void _startProvisioningAp();
    bool _configureSecureClient(WiFiClientSecure& client) const;
    String _provisioningApSsid() const;
#endif

    CloudCommandCallback _commandCb;

#ifdef NATIVE_TEST
    bool _mockWiFiConnected = true;
    bool _mockMqttConnected = true;
    std::vector<HttpResponse> _mockResponses;
    std::vector<std::pair<std::string, std::string>> _publishedMessages;
    std::vector<std::string> _httpRequestBodies;
#else
    WiFiClientSecure  _wifiClient;
    PubSubClient      _mqtt;
    unsigned long     _lastMqttReconnect = 0;
    bool              _otaEnabled        = false;
#endif
};
