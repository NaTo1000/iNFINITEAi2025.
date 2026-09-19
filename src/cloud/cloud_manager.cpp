// =============================================================================
// cloud_manager.cpp — WiFi, MQTT, HTTPS, and OTA implementation
// =============================================================================
#include "cloud_manager.h"

#ifndef NATIVE_TEST
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_system.h>
#else
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

CloudManager::CloudManager()
#ifndef NATIVE_TEST
    : _mqtt(_wifiClient)
#endif
{}

bool CloudManager::begin() {
#ifndef NATIVE_TEST
    _connectWifi();
    if (!isWiFiConnected()) {
        ESP_LOGW(LOG_TAG_CLOUD, "WiFi unavailable or not provisioned; starting provisioning AP");
        _startProvisioningAp();
        return false;
    }

    if (!hasTrustedTlsConfig()) {
        ESP_LOGE(LOG_TAG_CLOUD, "TLS root CA not provisioned; MQTT/HTTPS disabled");
        return false;
    }

    _configureSecureClient(_wifiClient);
    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback([this](const char* t, uint8_t* p, unsigned int l) {
        _mqttCallback(t, p, l);
    });
    _mqtt.setBufferSize(MQTT_MAX_PAYLOAD_BYTES + 64U);
    _connectMqtt();

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ESP_LOGI(LOG_TAG_CLOUD, "SNTP sync started");

    enableOta();
#endif
    return isWiFiConnected();
}

void CloudManager::loop() {
#ifndef NATIVE_TEST
    if (!_mqtt.connected() && isWiFiConnected()) {
        const unsigned long now = millis();
        if (now - _lastMqttReconnect >= MQTT_RECONNECT_DELAY_MS) {
            _lastMqttReconnect = now;
            _connectMqtt();
        }
    }
    if (_mqtt.connected()) {
        _mqtt.loop();
    }
    if (_otaEnabled) {
        ArduinoOTA.handle();
    }
#endif
}

bool CloudManager::isWiFiConnected() const {
#ifdef NATIVE_TEST
    return _mockWiFiConnected;
#else
    return WiFi.status() == WL_CONNECTED;
#endif
}

bool CloudManager::isMqttConnected() const {
#ifdef NATIVE_TEST
    return _mockMqttConnected;
#else
    return _mqtt.connected();
#endif
}

bool CloudManager::credentialsProvisioned() const {
    return !_isUnset(WIFI_SSID) && !_isUnset(WIFI_PASSWORD) &&
           !_isUnset(API_AUTH_TOKEN) && !_isUnset(MQTT_COMMAND_TOKEN);
}

bool CloudManager::hasTrustedTlsConfig() const {
    return !_isUnset(TLS_ROOT_CA);
}

bool CloudManager::publish(const std::string& topic, const std::string& payload) {
    if (payload.size() > MQTT_MAX_PAYLOAD_BYTES) {
        ESP_LOGW(LOG_TAG_CLOUD, "Dropping oversized publish payload (%u bytes)",
                 static_cast<unsigned>(payload.size()));
        return false;
    }
#ifdef NATIVE_TEST
    _publishedMessages.emplace_back(topic, payload);
    return _mockMqttConnected;
#else
    if (!isMqttConnected()) {
        return false;
    }
    return _mqtt.publish(topic.c_str(), payload.c_str(), false);
#endif
}

bool CloudManager::publishStatus(const std::string& jsonPayload) {
    return publish(MQTT_TOPIC_STATUS, jsonPayload);
}

bool CloudManager::publishAiResult(const std::string& jsonPayload) {
    return publish(MQTT_TOPIC_AI, jsonPayload);
}

bool CloudManager::publishInnovatorResult(const std::string& jsonPayload) {
    return publish(MQTT_TOPIC_INNO, jsonPayload);
}

HttpResponse CloudManager::httpPost(const std::string& url,
                                    const std::string& bearerToken,
                                    const std::string& jsonBody) {
    HttpResponse response;
#ifdef NATIVE_TEST
    (void)url;
    (void)bearerToken;
    (void)jsonBody;
    if (_mockResponses.empty()) {
        response.error = "no mock HTTP response queued";
        return response;
    }
    response = _mockResponses.front();
    _mockResponses.erase(_mockResponses.begin());
    if (response.attempts == 0) {
        response.attempts = 1;
    }
    return response;
#else
    if (url.empty()) {
        response.error = "AI API URL is not configured";
        return response;
    }
    if (!hasTrustedTlsConfig()) {
        response.error = "TLS root CA is not configured";
        return response;
    }

    for (uint8_t attempt = 1; attempt <= HTTP_RETRY_COUNT; ++attempt) {
        WiFiClientSecure client;
        if (!_configureSecureClient(client)) {
            response.error = "failed to configure TLS client";
            return response;
        }

        HTTPClient http;
        http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
        http.setTimeout(HTTP_REQUEST_TIMEOUT_MS);
        if (!http.begin(client, url.c_str())) {
            response.error = "failed to initialize HTTP client";
            response.attempts = attempt;
            break;
        }

        http.addHeader("Content-Type", "application/json");
        if (!bearerToken.empty()) {
            http.addHeader("Authorization", ("Bearer " + bearerToken).c_str());
        }

        const int statusCode = http.POST(reinterpret_cast<const uint8_t*>(jsonBody.data()), jsonBody.size());
        response.attempts = attempt;
        response.statusCode = statusCode;
        if (statusCode > 0) {
            String body = http.getString();
            if (body.length() > HTTP_MAX_RESPONSE_BYTES) {
                response.error = "HTTP response exceeded configured size limit";
            } else {
                response.body = body.c_str();
            }
            response.success = (statusCode >= 200 && statusCode < 300 && response.error.empty());
            ESP_LOGI(LOG_TAG_CLOUD, "HTTP POST attempt %u -> %d", attempt, statusCode);
            http.end();
            if (response.success || !_isRetryableHttpCode(statusCode)) {
                if (!response.success && response.error.empty()) {
                    response.error = "HTTP request returned non-success status";
                }
                return response;
            }
        } else {
            response.error = http.errorToString(statusCode).c_str();
            ESP_LOGW(LOG_TAG_CLOUD, "HTTP POST transport failure on attempt %u: %s",
                     attempt, response.error.c_str());
            http.end();
        }

        if (attempt < HTTP_RETRY_COUNT) {
            delay(250UL * attempt);
        }
    }

    if (response.error.empty()) {
        response.error = "HTTP request failed after retries";
    }
    return response;
#endif
}

void CloudManager::enableOta() {
#ifndef NATIVE_TEST
    if (_isUnset(OTA_PASSWORD_HASH)) {
        ESP_LOGW(LOG_TAG_CLOUD, "OTA disabled until OTA_PASSWORD_HASH is provisioned");
        _otaEnabled = false;
        return;
    }
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPasswordHash(OTA_PASSWORD_HASH);
    ArduinoOTA.onStart([]() {
        ESP_LOGI(LOG_TAG_CLOUD, "OTA start");
    });
    ArduinoOTA.onEnd([]() {
        ESP_LOGI(LOG_TAG_CLOUD, "OTA end");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        ESP_LOGE(LOG_TAG_CLOUD, "OTA error %u", e);
    });
    ArduinoOTA.begin();
    _otaEnabled = true;
    ESP_LOGI(LOG_TAG_CLOUD, "Authenticated OTA ready on %s", OTA_HOSTNAME);
#endif
}

bool CloudManager::_isUnset(const char* value) const {
    if (value == nullptr || value[0] == '\0') {
        return true;
    }
    const std::string v(value);
    return v.find("replace-me") != std::string::npos ||
           v.find("YOUR_") != std::string::npos;
}

bool CloudManager::_isRetryableHttpCode(int statusCode) const {
    return statusCode == 408 || statusCode == 425 || statusCode == 429 ||
           (statusCode >= 500 && statusCode < 600) || statusCode <= 0;
}

#ifdef NATIVE_TEST
void CloudManager::queueMockHttpResponse(const HttpResponse& response) {
    _mockResponses.push_back(response);
}

void CloudManager::setMockConnectivity(bool wifiConnected, bool mqttConnected) {
    _mockWiFiConnected = wifiConnected;
    _mockMqttConnected = mqttConnected;
}
#else
void CloudManager::_connectWifi() {
    if (_isUnset(WIFI_SSID) || _isUnset(WIFI_PASSWORD)) {
        ESP_LOGW(LOG_TAG_CLOUD, "WiFi credentials are not provisioned");
        return;
    }

    ESP_LOGI(LOG_TAG_CLOUD, "Connecting WiFi to configured SSID");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(LOG_TAG_CLOUD, "WiFi connected");
    }
}

void CloudManager::_connectMqtt() {
    if (_isUnset(MQTT_BROKER) || _isUnset(MQTT_USER) || _isUnset(MQTT_PASSWORD)) {
        ESP_LOGW(LOG_TAG_CLOUD, "MQTT credentials are not provisioned");
        return;
    }

    if (_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
        ESP_LOGI(LOG_TAG_CLOUD, "MQTT connected");
        _mqtt.subscribe(MQTT_TOPIC_CMD);
    } else {
        ESP_LOGW(LOG_TAG_CLOUD, "MQTT connect failed, rc=%d", _mqtt.state());
    }
}

void CloudManager::_mqttCallback(const char* topic,
                                 const uint8_t* payload,
                                 unsigned int len) {
    if (len == 0 || len > MQTT_MAX_PAYLOAD_BYTES) {
        ESP_LOGW(LOG_TAG_CLOUD, "Rejected MQTT payload with invalid size (%u bytes)", len);
        return;
    }

    std::string msg(reinterpret_cast<const char*>(payload), len);
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, msg.c_str());
    if (err || !doc.is<JsonObject>() || !doc["action"].is<const char*>()) {
        ESP_LOGW(LOG_TAG_CLOUD, "Rejected malformed MQTT command payload");
        return;
    }

    if (_commandCb) {
        _commandCb(std::string(topic), msg);
    }
}

void CloudManager::_startProvisioningAp() {
    const String ssid = _provisioningApSsid();
    const String password = _isUnset(WIFI_AP_PASSWORD)
        ? String("cfg") + String(static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL), HEX) + "!"
        : String(WIFI_AP_PASSWORD);
    WiFi.softAP(ssid.c_str(), password.c_str());
    ESP_LOGW(LOG_TAG_CLOUD, "Provisioning AP enabled as %s", ssid.c_str());
}

bool CloudManager::_configureSecureClient(WiFiClientSecure& client) const {
    if (_isUnset(TLS_ROOT_CA)) {
        return false;
    }
    client.setCACert(TLS_ROOT_CA);
    client.setTimeout(HTTP_REQUEST_TIMEOUT_MS / 1000UL);
    return true;
}

String CloudManager::_provisioningApSsid() const {
    char suffix[9];
    snprintf(suffix, sizeof(suffix), "%08llX", ESP.getEfuseMac() & 0xFFFFFFFFULL);
    return String(WIFI_AP_SSID_PREFIX) + suffix;
}
#endif
