// =============================================================================
// mobile_api.cpp — Async HTTP REST server + BLE GATT server
// =============================================================================
#include "mobile_api.h"
#include "../cloud/cloud_manager.h"
#include "../ai/ai_controller.h"
#include "../flipper/flipper_bridge.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <esp_log.h>

static AsyncWebServer webServer(API_PORT);

// BLE globals
static NimBLECharacteristic* g_cmdChar = nullptr;
static NimBLECharacteristic* g_rspChar = nullptr;
static MobileApi* g_apiInstance        = nullptr;

// BLE write callback
class BleWriteCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        if (g_apiInstance) {
            std::string val = c->getValue();
            g_apiInstance->_onBleWrite(val);
        }
    }
};
static BleWriteCallback g_bleWriteCb;
#else
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#endif

MobileApi::MobileApi(CloudManager& cloud, AiController& ai, FlipperBridge& flipper)
    : _cloud(cloud), _ai(ai), _flipper(flipper) {
#ifndef NATIVE_TEST
    g_apiInstance = this;
#endif
}

// -----------------------------------------------------------------------------
bool MobileApi::begin() {
    _setupBle();
    _setupRoutes();
#ifndef NATIVE_TEST
    webServer.begin();
    ESP_LOGI(LOG_TAG_MOBILE, "REST API listening on port %d", API_PORT);
#endif
    return true;
}

void MobileApi::loop() {
    // AsyncWebServer is interrupt-driven; BLE handled by NimBLE task
}

// -----------------------------------------------------------------------------
void MobileApi::notifyBle(const std::string& json) {
#ifndef NATIVE_TEST
    if (g_rspChar) {
        g_rspChar->setValue(json);
        g_rspChar->notify();
    }
#else
    printf("[MOBILE] BLE notify: %s\n", json.c_str());
#endif
}

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
bool MobileApi::_authenticate(const std::string& authHeader) const {
    return authHeader == std::string("Bearer ") + API_AUTH_TOKEN;
}

void MobileApi::_onBleWrite(const std::string& data) {
    ESP_LOGI(LOG_TAG_MOBILE, "BLE cmd: %s", data.c_str());
    if (_cmdCb) _cmdCb(data);
}

// -----------------------------------------------------------------------------
void MobileApi::_setupBle() {
#ifndef NATIVE_TEST
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEServer* server = NimBLEDevice::createServer();
    NimBLEService* svc   = server->createService(BLE_SERVICE_UUID);

    g_cmdChar = svc->createCharacteristic(
        BLE_CMD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_cmdChar->setCallbacks(&g_bleWriteCb);

    g_rspChar = svc->createCharacteristic(
        BLE_RSP_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->start();
    ESP_LOGI(LOG_TAG_MOBILE, "BLE advertising as '%s'", BLE_DEVICE_NAME);
#endif
}

// -----------------------------------------------------------------------------
void MobileApi::_setupRoutes() {
#ifndef NATIVE_TEST
    // ---------- GET /status ----------
    webServer.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!_authenticate(req->header("Authorization").c_str())) {
            req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        JsonDocument doc;
        doc["device"]  = DEVICE_NAME;
        doc["version"] = DEVICE_VERSION;
        doc["wifi"]    = _cloud.isWiFiConnected();
        doc["mqtt"]    = _cloud.isMqttConnected();
        doc["flipper"] = _flipper.isConnected();
        std::string body;
        serializeJson(doc, body);
        req->send(200, "application/json", body.c_str());
    });

    // ---------- POST /command ----------
    webServer.on("/command", HTTP_POST,
        [](AsyncWebServerRequest* req) {},  // handled in body callback
        nullptr,
        [this](AsyncWebServerRequest* req,
               uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (!_authenticate(req->header("Authorization").c_str())) {
                req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            std::string body(reinterpret_cast<char*>(data), len);
            ESP_LOGI(LOG_TAG_MOBILE, "REST /command: %s", body.c_str());
            if (_cmdCb) _cmdCb(body);
            req->send(200, "application/json", "{\"status\":\"queued\"}");
        });

    // ---------- POST /ai/query ----------
    webServer.on("/ai/query", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req,
               uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (!_authenticate(req->header("Authorization").c_str())) {
                req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            std::string body(reinterpret_cast<char*>(data), len);
            JsonDocument reqDoc;
            deserializeJson(reqDoc, body.c_str());
            AiRequest aiReq;
            aiReq.context    = reqDoc["context"] | "";
            aiReq.systemRole = reqDoc["system"]  | "";
            AiResponse resp  = _ai.query(aiReq);
            JsonDocument respDoc;
            respDoc["success"]  = resp.success;
            respDoc["decision"] = resp.decision.c_str();
            std::string respBody;
            serializeJson(respDoc, respBody);
            req->send(200, "application/json", respBody.c_str());
        });

    // ---------- 404 ----------
    webServer.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"not found\"}");
    });
#endif
}
