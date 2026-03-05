// =============================================================================
// cloud_manager.cpp — WiFi, MQTT, REST and OTA implementation
// =============================================================================
#ifndef NATIVE_TEST

#include "cloud_manager.h"
#include <esp_log.h>

CloudManager::CloudManager()
    : _mqtt(_wifiClient) {}

// -----------------------------------------------------------------------------
bool CloudManager::begin() {
    _connectWifi();
    if (!isWiFiConnected()) {
        ESP_LOGW(LOG_TAG_CLOUD, "WiFi failed — starting AP mode");
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        return false;
    }

    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback([this](const char* t, uint8_t* p, unsigned int l) {
        _mqttCallback(t, p, l);
    });
    _mqtt.setBufferSize(1024);
    _connectMqtt();

    // Sync system clock via SNTP so ProcedureStore timestamps are accurate
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ESP_LOGI(LOG_TAG_CLOUD, "SNTP sync started");

    enableOta();
    return true;
}

// -----------------------------------------------------------------------------
void CloudManager::loop() {
    // Keep MQTT alive
    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastMqttReconnect > MQTT_RECONNECT_DELAY_MS) {
            _lastMqttReconnect = now;
            _connectMqtt();
        }
    }
    _mqtt.loop();

    if (_otaEnabled) ArduinoOTA.handle();
}

// -----------------------------------------------------------------------------
bool CloudManager::isWiFiConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool CloudManager::isMqttConnected() const {
    return _mqtt.connected();
}

// -----------------------------------------------------------------------------
bool CloudManager::publish(const std::string& topic, const std::string& payload) {
    if (!isMqttConnected()) return false;
    return _mqtt.publish(topic.c_str(), payload.c_str(), /*retain=*/false);
}

bool CloudManager::publishStatus(const std::string& json) {
    return publish(MQTT_TOPIC_STATUS, json);
}

bool CloudManager::publishAiResult(const std::string& json) {
    return publish(MQTT_TOPIC_AI, json);
}

bool CloudManager::publishInnovatorResult(const std::string& json) {
    return publish(MQTT_TOPIC_INNO, json);
}

// -----------------------------------------------------------------------------
std::string CloudManager::httpPost(const std::string& url,
                                   const std::string& bearerToken,
                                   const std::string& body) {
    HTTPClient http;
    http.begin(url.c_str());
    http.addHeader("Content-Type", "application/json");
    if (!bearerToken.empty())
        http.addHeader("Authorization", ("Bearer " + bearerToken).c_str());

    int code = http.POST(body.c_str());
    std::string response;
    if (code > 0) {
        response = http.getString().c_str();
        ESP_LOGI(LOG_TAG_CLOUD, "HTTP POST %s → %d", url.c_str(), code);
    } else {
        ESP_LOGE(LOG_TAG_CLOUD, "HTTP POST %s failed: %s",
                 url.c_str(), http.errorToString(code).c_str());
    }
    http.end();
    return response;
}

// -----------------------------------------------------------------------------
void CloudManager::enableOta() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.onStart([]() {
        ESP_LOGI(LOG_TAG_CLOUD, "OTA start");
    });
    ArduinoOTA.onEnd([]() {
        ESP_LOGI(LOG_TAG_CLOUD, "OTA end — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        ESP_LOGE(LOG_TAG_CLOUD, "OTA error %u", e);
    });
    ArduinoOTA.begin();
    _otaEnabled = true;
    ESP_LOGI(LOG_TAG_CLOUD, "OTA ready on %s", OTA_HOSTNAME);
}

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
void CloudManager::_connectWifi() {
    ESP_LOGI(LOG_TAG_CLOUD, "Connecting WiFi → %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(LOG_TAG_CLOUD, "WiFi connected — IP: %s",
                 WiFi.localIP().toString().c_str());
    }
}

void CloudManager::_connectMqtt() {
    if (_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
        ESP_LOGI(LOG_TAG_CLOUD, "MQTT connected to %s", MQTT_BROKER);
        _mqtt.subscribe(MQTT_TOPIC_CMD);
    } else {
        ESP_LOGW(LOG_TAG_CLOUD, "MQTT connect failed, rc=%d", _mqtt.state());
    }
}

void CloudManager::_mqttCallback(const char* topic,
                                  const uint8_t* payload,
                                  unsigned int len) {
    std::string msg(reinterpret_cast<const char*>(payload), len);
    ESP_LOGI(LOG_TAG_CLOUD, "MQTT msg [%s]: %s", topic, msg.c_str());
    if (_commandCb) _commandCb(std::string(topic), msg);
}

#endif // NATIVE_TEST
