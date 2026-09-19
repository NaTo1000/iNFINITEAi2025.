// =============================================================================
// ai_controller.cpp — AI control module implementation
// =============================================================================
#include "ai_controller.h"
#include "../cloud/cloud_manager.h"
#include "../common/native_json.h"

#ifndef NATIVE_TEST
#include <ArduinoJson.h>
#include <esp_log.h>
#else
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

namespace {
bool isAllowedProcedureAction(const std::string& action) {
    return action == "log" || action == "publish_status" ||
           action == "wait_ms" || action == "request_review";
}
} // namespace

AiController::AiController(CloudManager& cloud)
    : _cloud(cloud) {}

AiResponse AiController::query(const AiRequest& req) {
    AiResponse resp;
    const std::string payload = _buildPayload(req);
    ESP_LOGI(LOG_TAG_AI, "Querying AI model '%s'", AI_MODEL);

    const HttpResponse httpResp = _cloud.httpPost(AI_API_URL, AI_API_KEY, payload);
    resp.raw = httpResp.body;
    if (!httpResp.success) {
        resp.reason = httpResp.error.empty()
            ? "AI transport or provider request failed"
            : httpResp.error;
        ESP_LOGE(LOG_TAG_AI, "AI request failed: %s", resp.reason.c_str());
        return resp;
    }

    const std::string content = _extractContent(httpResp.body);
    if (content.empty()) {
        resp.reason = "Provider response did not contain a usable content field";
        ESP_LOGE(LOG_TAG_AI, "%s", resp.reason.c_str());
        return resp;
    }

    resp = _parseStructuredDecision(content);
    resp.raw = httpResp.body;
    if (resp.success) {
        if (_decisionCb && !resp.decision.empty()) {
            _decisionCb(resp.decision);
        }
    }
    return resp;
}

AiResponse AiController::diagnose(const std::string& errorDescription) {
    AiRequest req;
    req.systemRole =
        "You are an embedded-systems safety reviewer. Return STRICT JSON only. "
        "Use verdict=PROPOSE and include summary plus procedure.steps with only "
        "allowlisted actions: log, publish_status, wait_ms, request_review. "
        "Include at least one validation criterion. Never output code.";
    req.context =
        "Task: " + errorDescription +
        "\nRespond with {\"verdict\":\"PROPOSE\",\"summary\":string,"
        "\"procedure\":{\"title\":string,\"steps\":[{\"action\":string,"
        "\"value\":string,\"valueNumber\":number}],\"validation\":[string]}}";
    return query(req);
}

AiResponse AiController::evaluate(const std::string& testResult) {
    AiRequest req;
    req.systemRole =
        "You are a firmware quality reviewer. Return STRICT JSON only with an exact "
        "verdict field of PASS or FAIL and a short reason. Do not include code.";
    req.context =
        "Evaluation target: " + testResult +
        "\nRespond with {\"verdict\":\"PASS|FAIL\",\"reason\":string}.";
    return query(req);
}

std::string AiController::_buildPayload(const AiRequest& req) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["model"] = AI_MODEL;
    doc["max_tokens"] = AI_MAX_TOKENS;
    doc["temperature"] = AI_TEMP;
    doc["messages"][0]["role"] = "system";
    doc["messages"][0]["content"] = req.systemRole.empty()
        ? "You are a helpful embedded-systems AI assistant."
        : req.systemRole.c_str();
    doc["messages"][1]["role"] = "user";
    doc["messages"][1]["content"] = req.context.c_str();
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    return std::string("{\"model\":\"") + AI_MODEL +
           "\",\"messages\":[{\"role\":\"system\",\"content\":\"" +
           nativejson::escapeString(req.systemRole) +
           "\"},{\"role\":\"user\",\"content\":\"" +
           nativejson::escapeString(req.context) + "\"}]}";
#endif
}

std::string AiController::_extractContent(const std::string& rawJson) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, rawJson.c_str());
    if (err) {
        ESP_LOGE(LOG_TAG_AI, "JSON parse error: %s", err.c_str());
        return {};
    }
    JsonVariant content = doc["choices"][0]["message"]["content"];
    if (content.is<const char*>()) {
        return std::string(content.as<const char*>());
    }
    if (content.is<JsonArray>()) {
        std::string assembled;
        for (JsonVariant item : content.as<JsonArray>()) {
            if (item["text"].is<const char*>()) {
                assembled += item["text"].as<const char*>();
            }
        }
        return assembled;
    }
    return {};
#else
    return rawJson;
#endif
}

AiResponse AiController::_parseStructuredDecision(const std::string& content) const {
    AiResponse resp;
#ifndef NATIVE_TEST
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, content.c_str());
    if (err || !doc.is<JsonObject>()) {
        resp.reason = "Structured AI response was not valid JSON";
        return resp;
    }

    std::string verdictText = doc["verdict"] | "";
    resp.verdict = _parseVerdict(verdictText);
    resp.decision = doc["summary"] | doc["reason"] | "";
    resp.reason = doc["reason"] | "";

    JsonVariant proc = doc["procedure"];
    if (proc.is<JsonObject>()) {
        resp.procedure.title = proc["title"] | "";
        resp.procedure.summary = doc["summary"] | "";
        for (JsonVariant step : proc["steps"].as<JsonArray>()) {
            AiProcedureStep parsed;
            parsed.action = step["action"] | "";
            parsed.value = step["value"] | "";
            parsed.valueNumber = step["valueNumber"] | 0U;
            resp.procedure.steps.push_back(parsed);
        }
        for (JsonVariant validation : proc["validation"].as<JsonArray>()) {
            if (validation.is<const char*>()) {
                resp.procedure.validation.emplace_back(validation.as<const char*>());
            }
        }
    }
#else
    std::string verdictText;
    if (!nativejson::extractStringField(content, "verdict", verdictText)) {
        resp.reason = "Structured AI response is missing verdict";
        return resp;
    }
    resp.verdict = _parseVerdict(verdictText);
    nativejson::extractStringField(content, "summary", resp.decision);
    nativejson::extractStringField(content, "reason", resp.reason);
    if (resp.decision.empty()) {
        resp.decision = resp.reason;
    }

    std::string procedureJson;
    if (nativejson::extractObjectField(content, "procedure", procedureJson)) {
        nativejson::extractStringField(procedureJson, "title", resp.procedure.title);
        resp.procedure.summary = resp.decision;
        std::string stepArray;
        if (nativejson::extractArrayField(procedureJson, "steps", stepArray)) {
            for (const std::string& stepJson : nativejson::splitObjectArray(stepArray)) {
                AiProcedureStep step;
                nativejson::extractStringField(stepJson, "action", step.action);
                nativejson::extractStringField(stepJson, "value", step.value);
                nativejson::extractUIntField(stepJson, "valueNumber", step.valueNumber);
                resp.procedure.steps.push_back(step);
            }
        }
        std::string validationArray;
        if (nativejson::extractArrayField(procedureJson, "validation", validationArray)) {
            resp.procedure.validation = nativejson::extractStringArrayValues(validationArray);
        }
    }
#endif

    if (resp.verdict == AiVerdict::PROPOSE) {
        std::string sanitizeReason;
        if (!_sanitizeProcedure(resp.procedure, sanitizeReason)) {
            resp.reason = sanitizeReason;
            resp.success = false;
            return resp;
        }
    }

    if (resp.verdict == AiVerdict::PASS || resp.verdict == AiVerdict::FAIL) {
        resp.success = !resp.reason.empty() || !resp.decision.empty();
        if (resp.decision.empty()) {
            resp.decision = resp.reason;
        }
        return resp;
    }

    resp.success = (resp.verdict == AiVerdict::PROPOSE);
    if (!resp.success && resp.reason.empty()) {
        resp.reason = "Unsupported AI verdict";
    }
    return resp;
}

bool AiController::_sanitizeProcedure(AiProcedurePlan& procedure, std::string& reason) const {
    if (procedure.steps.empty() || procedure.steps.size() > AI_MAX_STEPS) {
        reason = "Procedure must contain between 1 and 8 allowlisted steps";
        return false;
    }
    if (procedure.validation.empty() || procedure.validation.size() > AI_MAX_VALIDATION_RULES) {
        reason = "Procedure must include explicit validation criteria";
        return false;
    }
    for (AiProcedureStep& step : procedure.steps) {
        if (!isAllowedProcedureAction(step.action)) {
            reason = "Procedure contains a non-allowlisted action";
            return false;
        }
        if (step.action == "wait_ms") {
            if (step.valueNumber > INNOVATOR_MAX_STEP_DELAY_MS) {
                reason = "wait_ms step exceeded the maximum delay";
                return false;
            }
        } else if (step.value.size() > PROCEDURE_MAX_DESCRIPTION_BYTES) {
            reason = "Procedure step value exceeded the maximum size";
            return false;
        }
    }
    return true;
}

AiVerdict AiController::_parseVerdict(const std::string& verdictText) {
    if (verdictText == "PROPOSE") return AiVerdict::PROPOSE;
    if (verdictText == "PASS") return AiVerdict::PASS;
    if (verdictText == "FAIL") return AiVerdict::FAIL;
    if (verdictText == "ERROR") return AiVerdict::ERROR;
    return AiVerdict::NONE;
}

#ifdef NATIVE_TEST
AiResponse AiController::parseStructuredDecisionForTest(const std::string& content) {
    struct DummyCloud : CloudManager {};
    DummyCloud cloud;
    AiController controller(cloud);
    return controller._parseStructuredDecision(content);
}
#endif
