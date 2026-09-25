// =============================================================================
// mobile_api.cpp — Async HTTP REST server + BLE GATT server
// =============================================================================
#include "mobile_api.h"
#include "../ai/ai_controller.h"
#include "../cloud/cloud_manager.h"
#include "../flipper/flipper_bridge.h"
#include "../innovator/firmware_innovator.h"
#include "../procedures/procedure_store.h"
#include "../common/native_json.h"
#include <algorithm>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <NimBLEDevice.h>
#include <esp_log.h>
static AsyncWebServer webServer(API_PORT);
static NimBLECharacteristic* g_cmdChar = nullptr;
static NimBLECharacteristic* g_rspChar = nullptr;
static MobileApi* g_apiInstance = nullptr;

class BleWriteCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        if (g_apiInstance) {
            g_apiInstance->_onBleWrite(c->getValue());
        }
    }
};
static BleWriteCallback g_bleWriteCb;
#else
#include <chrono>
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

MobileApi::MobileApi(CloudManager& cloud,
                     AiController& ai,
                     FlipperBridge& flipper,
                     ProcedureStore& store,
                     FirmwareInnovator& innovator)
    : _cloud(cloud), _ai(ai), _flipper(flipper), _store(store), _innovator(innovator) {
#ifndef NATIVE_TEST
    g_apiInstance = this;
#endif
}

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
    // Async HTTP and BLE are event driven.
}

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

bool MobileApi::_authenticate(const std::string& authHeader) const {
    return !std::string(API_AUTH_TOKEN).empty() &&
           std::string(API_AUTH_TOKEN).size() >= 12U &&
           authHeader == std::string("Bearer ") + API_AUTH_TOKEN;
}

bool MobileApi::_rateLimitOk() {
    const uint32_t now = _nowMs();
    _recentCallTimes.erase(
        std::remove_if(_recentCallTimes.begin(), _recentCallTimes.end(), [now](uint32_t ts) {
            return now - ts > API_RATE_LIMIT_WINDOW_MS;
        }),
        _recentCallTimes.end());
    if (_recentCallTimes.size() >= API_RATE_LIMIT_MAX_CALLS) {
        return false;
    }
    _recentCallTimes.push_back(now);
    return true;
}

void MobileApi::_onBleWrite(const std::string& data) {
    std::string auth;
    if (!nativejson::extractStringField(data, "auth", auth) ||
        auth != API_AUTH_TOKEN || !_rateLimitOk()) {
        ESP_LOGW(LOG_TAG_MOBILE, "%s", "Rejected BLE command lacking valid auth or rate limit");
        return;
    }
    if (_cmdCb) {
        _cmdCb(data);
    }
}

void MobileApi::_setupBle() {
#ifndef NATIVE_TEST
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityPasskey(BLE_PAIRING_PASSKEY);
    NimBLEServer* server = NimBLEDevice::createServer();
    NimBLEService* svc = server->createService(BLE_SERVICE_UUID);

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
    ESP_LOGI(LOG_TAG_MOBILE, "BLE advertising with authenticated pairing enabled");
#endif
}

void MobileApi::_setupRoutes() {
#ifndef NATIVE_TEST
    webServer.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!_authenticate(req->header("Authorization").c_str())) {
            req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        JsonDocument doc;
        doc["device"] = DEVICE_NAME;
        doc["version"] = DEVICE_VERSION;
        doc["wifi"] = _cloud.isWiFiConnected();
        doc["mqtt"] = _cloud.isMqttConnected();
        doc["flipper"] = _flipper.isConnected();
        doc["innovator_busy"] = _innovator.isBusy();
        doc["procedure_count"] = _store.count();
        std::string body;
        serializeJson(doc, body);
        req->send(200, "application/json", body.c_str());
    });

    webServer.on("/procedures", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!_authenticate(req->header("Authorization").c_str())) {
            req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        req->send(200, "application/json", _store.toJson().c_str());
    });

    webServer.on("/innovate/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (!_authenticate(req->header("Authorization").c_str())) {
            req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        JsonDocument doc;
        doc["busy"] = _innovator.isBusy();
        doc["stage"] = _innovator.currentStage().c_str();
        doc["task"] = _innovator.currentTaskId().c_str();
        doc["iterations"] = _innovator.currentIterations();
        doc["summary"] = _innovator.lastStatusSummary().c_str();
        doc["log"] = _innovator.getLog().c_str();
        if (!_innovator.lastReportJson().empty()) {
            JsonDocument reportDoc;
            if (!deserializeJson(reportDoc, _innovator.lastReportJson().c_str())) {
                doc["report"] = reportDoc.as<JsonObject>();
            }
        }
        std::string body;
        serializeJson(doc, body);
        req->send(200, "application/json", body.c_str());
    });

    webServer.on("/command", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_authenticate(req->header("Authorization").c_str())) {
                req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (!_rateLimitOk()) {
                req->send(429, "application/json", "{\"error\":\"rate_limited\"}");
                return;
            }
            std::string body;
            std::string error;
            if (!_appendBodyChunk(_requestKey(req), data, len, index, total, body, error)) {
                req->send(413, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
                return;
            }
            if (!body.empty()) {
                JsonDocument cmd;
                if (deserializeJson(cmd, body.c_str()) || !cmd.is<JsonObject>() || !cmd["action"].is<const char*>()) {
                    req->send(400, "application/json", "{\"error\":\"invalid_command_json\"}");
                    return;
                }
                if (_cmdCb) {
                    _cmdCb(body);
                }
                req->send(202, "application/json", "{\"status\":\"queued\"}");
            }
        });

    webServer.on("/innovate", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_authenticate(req->header("Authorization").c_str())) {
                req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (!_rateLimitOk()) {
                req->send(429, "application/json", "{\"error\":\"rate_limited\"}");
                return;
            }
            std::string body;
            std::string error;
            if (!_appendBodyChunk(_requestKey(req), data, len, index, total, body, error)) {
                req->send(413, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
                return;
            }
            if (!body.empty()) {
                JsonDocument doc;
                if (deserializeJson(doc, body.c_str()) || !doc.is<JsonObject>()) {
                    req->send(400, "application/json", "{\"error\":\"invalid_json\"}");
                    return;
                }
                const char* description = doc["description"] | "";
                if (description[0] == '\0') {
                    req->send(400, "application/json", "{\"error\":\"description_required\"}");
                    return;
                }
                static uint32_t seq = 0;
                InnovationTask task;
                task.id = "api_" + std::to_string(++seq);
                task.description = description;
                task.context = body;
                _innovator.queueTask(task);
                JsonDocument resp;
                resp["status"] = "queued";
                resp["task"] = task.id.c_str();
                std::string respBody;
                serializeJson(resp, respBody);
                req->send(202, "application/json", respBody.c_str());
            }
        });

    webServer.on("/ai/query", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_authenticate(req->header("Authorization").c_str())) {
                req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (!_rateLimitOk()) {
                req->send(429, "application/json", "{\"error\":\"rate_limited\"}");
                return;
            }
            std::string body;
            std::string error;
            if (!_appendBodyChunk(_requestKey(req), data, len, index, total, body, error)) {
                req->send(413, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
                return;
            }
            if (!body.empty()) {
                JsonDocument reqDoc;
                if (deserializeJson(reqDoc, body.c_str()) || !reqDoc.is<JsonObject>() || !reqDoc["context"].is<const char*>()) {
                    req->send(400, "application/json", "{\"error\":\"context_required\"}");
                    return;
                }
                AiRequest aiReq;
                aiReq.context = reqDoc["context"] | "";
                aiReq.systemRole = reqDoc["system"] | "";
                const AiResponse resp = _ai.query(aiReq);
                JsonDocument respDoc;
                respDoc["success"] = resp.success;
                const char* verdict = resp.verdict == AiVerdict::PASS ? "PASS" :
                    resp.verdict == AiVerdict::FAIL ? "FAIL" :
                    resp.verdict == AiVerdict::PROPOSE ? "PROPOSE" : "NONE";
                respDoc["verdict"] = verdict;
                respDoc["decision"] = resp.decision.c_str();
                respDoc["reason"] = resp.reason.c_str();
                std::string respBody;
                serializeJson(respDoc, respBody);
                req->send(resp.success ? 200 : 502, "application/json", respBody.c_str());
            }
        });

    webServer.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
#endif
}

std::string MobileApi::_requestKey(const void* request) const {
    return std::to_string(reinterpret_cast<uintptr_t>(request));
}

bool MobileApi::_appendBodyChunk(const std::string& requestKey,
                                 const uint8_t* data,
                                 size_t len,
                                 size_t index,
                                 size_t total,
                                 std::string& completeBody,
                                 std::string& error) {
    completeBody.clear();
    if (total == 0 || total > API_MAX_BODY_BYTES || index > total) {
        error = "body_too_large_or_invalid";
        _clearRequestBuffer(requestKey);
        return false;
    }
    auto existing = _requestBuffers.find(requestKey);
    if (existing == _requestBuffers.end() && index != 0) {
        error = "fragment_sequence_error";
        return false;
    }
    if (index + len > total) {
        error = "body_too_large_or_invalid";
        _clearRequestBuffer(requestKey);
        return false;
    }
    BufferedRequest& buffer = _requestBuffers[requestKey];
    if (index == 0) {
        buffer.data.clear();
        buffer.total = total;
        buffer.data.reserve(total);
    }
    if (buffer.total != total || index != buffer.data.size()) {
        error = "fragment_sequence_error";
        _clearRequestBuffer(requestKey);
        return false;
    }
    buffer.data.append(reinterpret_cast<const char*>(data), len);
    if (buffer.data.size() > API_MAX_BODY_BYTES) {
        error = "body_too_large_or_invalid";
        _clearRequestBuffer(requestKey);
        return false;
    }
    if (buffer.data.size() == total) {
        completeBody = buffer.data;
        _clearRequestBuffer(requestKey);
    }
    return true;
}

void MobileApi::_clearRequestBuffer(const std::string& requestKey) {
    _requestBuffers.erase(requestKey);
}

uint32_t MobileApi::_nowMs() const {
#ifndef NATIVE_TEST
    return static_cast<uint32_t>(millis());
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFFU);
#endif
}

#ifdef NATIVE_TEST
MobileApi::BodyChunkResult MobileApi::accumulateBodyChunkForTest(const std::string& requestId,
                                                                 const std::string& chunk,
                                                                 size_t index,
                                                                 size_t total) {
    BodyChunkResult result;
    result.accepted = _appendBodyChunk(requestId,
                                       reinterpret_cast<const uint8_t*>(chunk.data()),
                                       chunk.size(),
                                       index,
                                       total,
                                       result.body,
                                       result.error);
    result.complete = !result.body.empty();
    return result;
}
#endif
