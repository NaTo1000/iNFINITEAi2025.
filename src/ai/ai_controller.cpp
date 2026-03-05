// =============================================================================
// ai_controller.cpp — AI control module implementation
// =============================================================================
#include "ai_controller.h"
#include "../cloud/cloud_manager.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#else
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

AiController::AiController(CloudManager& cloud)
    : _cloud(cloud) {}

// -----------------------------------------------------------------------------
AiResponse AiController::query(const AiRequest& req) {
    AiResponse resp;
    std::string payload = _buildPayload(req);
    ESP_LOGI(LOG_TAG_AI, "Querying AI model: %s", AI_MODEL);

    resp.raw = _cloud.httpPost(AI_API_URL, AI_API_KEY, payload);
    if (resp.raw.empty()) {
        ESP_LOGE(LOG_TAG_AI, "Empty response from AI API");
        return resp;
    }

    resp.decision = _extractContent(resp.raw);
    resp.success  = !resp.decision.empty();

    if (resp.success) {
        ESP_LOGI(LOG_TAG_AI, "AI decision: %s", resp.decision.c_str());
        if (_decisionCb) _decisionCb(resp.decision);
    }
    return resp;
}

// -----------------------------------------------------------------------------
AiResponse AiController::diagnose(const std::string& errorDescription) {
    AiRequest req;
    req.systemRole = "You are an embedded-systems expert. "
                     "Given an ESP32 firmware error, provide a concise fix "
                     "as valid C++ code or a numbered action list. "
                     "Keep your answer under 200 words.";
    req.context    = "Error: " + errorDescription;
    return query(req);
}

// -----------------------------------------------------------------------------
AiResponse AiController::evaluate(const std::string& testResult) {
    AiRequest req;
    req.systemRole = "You are a firmware quality reviewer. "
                     "Assess the following test result and state whether it "
                     "meets peak standards (PASS) or needs more work (FAIL) "
                     "with a brief reason.";
    req.context    = "Test result: " + testResult;
    return query(req);
}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------
std::string AiController::_buildPayload(const AiRequest& req) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["model"]                        = AI_MODEL;
    doc["max_tokens"]                   = AI_MAX_TOKENS;
    doc["temperature"]                  = AI_TEMP;
    doc["messages"][0]["role"]          = "system";
    doc["messages"][0]["content"]       = req.systemRole.empty()
        ? "You are a helpful embedded-systems AI assistant."
        : req.systemRole.c_str();
    doc["messages"][1]["role"]          = "user";
    doc["messages"][1]["content"]       = req.context.c_str();
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    // Minimal stub for native unit tests
    return "{\"model\":\"" + std::string(AI_MODEL) + "\","
           "\"messages\":[{\"role\":\"user\",\"content\":\""
           + req.context + "\"}]}";
#endif
}

std::string AiController::_extractContent(const std::string& rawJson) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, rawJson.c_str());
    if (err) {
        ESP_LOGE(LOG_TAG_AI, "JSON parse error: %s", err.c_str());
        return {};
    }
    // OpenAI-compatible response format
    if (doc["choices"][0]["message"]["content"].is<const char*>()) {
        return std::string(doc["choices"][0]["message"]["content"].as<const char*>());
    }
    return {};
#else
    // For native tests, return a mock decision
    if (rawJson.find("PASS") != std::string::npos) return "PASS";
    if (rawJson.find("content") != std::string::npos) return "mock_decision";
    return {};
#endif
}
