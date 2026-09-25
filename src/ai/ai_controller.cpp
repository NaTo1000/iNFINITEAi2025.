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

bool isAllowedRecoveryCategory(const std::string& category) {
    return category == "legal_support" ||
           category == "labor_board" ||
           category == "safety_board" ||
           category == "internal_recovery" ||
           category == "no_action";
}

bool isPassLikeDecision(const std::string& decision) {
    return decision == "PASS";
}

bool isRebuildLikeDecision(const std::string& decision) {
    return decision == "REBUILD";
}

bool isScrapLikeDecision(const std::string& decision) {
    return decision == "SCRAP";
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
        "You are an embedded-systems evidence review coordinator. Return STRICT JSON only. "
        "Use verdict=PROPOSE and include summary, procedure.steps with only allowlisted actions "
        "log, publish_status, wait_ms, request_review, and a report object with evidence, "
        "analysis, reviewer outputs, scores, qcDecision, and bounded recovery actions. "
        "Every conclusion must be grounded in cited evidence. Never output code.";
    req.context =
        "Task: " + errorDescription +
        "\nRespond with {\"verdict\":\"PROPOSE\",\"summary\":string,"
        "\"procedure\":{\"title\":string,\"steps\":[{\"action\":string,"
        "\"value\":string,\"valueNumber\":number}],\"validation\":[string]},"
        "\"report\":{\"source\":{\"type\":string,\"label\":string},"
        "\"evidence\":[{\"id\":string,\"type\":string,\"source\":string,\"detail\":string}],"
        "\"analysis\":{\"normalizedSummary\":string,\"verifiedFacts\":[string],"
        "\"unsupportedClaims\":[string],\"biasNotes\":[string]},"
        "\"reviewers\":[{\"model\":string,\"verdict\":\"PASS|FAIL\",\"confidence\":number,"
        "\"notes\":string}],\"scores\":{\"quality\":number,\"confidence\":number,"
        "\"contradiction\":number,\"deception\":number},"
        "\"qcDecision\":\"PASS|REBUILD|SCRAP\","
        "\"recovery\":[{\"category\":\"legal_support|labor_board|safety_board|internal_recovery|no_action\","
        "\"detail\":string}],\"recommendationSummary\":string}}";
    return query(req);
}

AiResponse AiController::evaluate(const std::string& testResult) {
    AiRequest req;
    req.systemRole =
        "You are a firmware evidence quality reviewer. Return STRICT JSON only with an exact "
        "verdict field of PASS or FAIL, a short reason, and a report object. "
        "Use PASS only when the evidence is concrete, quality and confidence are each at least 90, "
        "unsupportedClaims is empty, and contradiction/deception scores are low. Do not include code.";
    req.context =
        "Evaluation target: " + testResult +
        "\nRespond with {\"verdict\":\"PASS|FAIL\",\"reason\":string,"
        "\"report\":{\"source\":{\"type\":string,\"label\":string},"
        "\"evidence\":[{\"id\":string,\"type\":string,\"source\":string,\"detail\":string}],"
        "\"analysis\":{\"normalizedSummary\":string,\"verifiedFacts\":[string],"
        "\"unsupportedClaims\":[string],\"biasNotes\":[string]},"
        "\"reviewers\":[{\"model\":string,\"verdict\":\"PASS|FAIL\",\"confidence\":number,"
        "\"notes\":string}],\"scores\":{\"quality\":number,\"confidence\":number,"
        "\"contradiction\":number,\"deception\":number},"
        "\"qcDecision\":\"PASS|REBUILD|SCRAP\","
        "\"recovery\":[{\"category\":\"legal_support|labor_board|safety_board|internal_recovery|no_action\","
        "\"detail\":string}],\"recommendationSummary\":string}}.";
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
    JsonVariant report = doc["report"];
    if (report.is<JsonObject>()) {
        JsonVariant source = report["source"];
        resp.report.sourceType = source["type"] | "";
        resp.report.sourceLabel = source["label"] | "";
        JsonVariant analysis = report["analysis"];
        resp.report.normalizedSummary = analysis["normalizedSummary"] | "";
        resp.report.recommendationSummary = report["recommendationSummary"] | "";
        JsonVariant scores = report["scores"];
        resp.report.qualityScore = scores["quality"] | 0U;
        resp.report.confidenceScore = scores["confidence"] | 0U;
        resp.report.contradictionScore = scores["contradiction"] | 0U;
        resp.report.deceptionScore = scores["deception"] | 0U;
        resp.report.qcDecision = report["qcDecision"] | "";
        for (JsonVariant item : report["evidence"].as<JsonArray>()) {
            AiEvidenceReference ref;
            ref.id = item["id"] | "";
            ref.type = item["type"] | "";
            ref.source = item["source"] | "";
            ref.detail = item["detail"] | "";
            resp.report.evidence.push_back(ref);
        }
        for (JsonVariant item : analysis["verifiedFacts"].as<JsonArray>()) {
            if (item.is<const char*>()) {
                resp.report.verifiedFacts.emplace_back(item.as<const char*>());
            }
        }
        for (JsonVariant item : analysis["unsupportedClaims"].as<JsonArray>()) {
            if (item.is<const char*>()) {
                resp.report.unsupportedClaims.emplace_back(item.as<const char*>());
            }
        }
        for (JsonVariant item : analysis["biasNotes"].as<JsonArray>()) {
            if (item.is<const char*>()) {
                resp.report.biasNotes.emplace_back(item.as<const char*>());
            }
        }
        for (JsonVariant item : report["reviewers"].as<JsonArray>()) {
            AiReviewerOutput reviewer;
            reviewer.model = item["model"] | "";
            reviewer.verdict = _parseVerdict(item["verdict"] | "");
            reviewer.confidence = item["confidence"] | 0U;
            reviewer.notes = item["notes"] | "";
            resp.report.reviewers.push_back(reviewer);
        }
        for (JsonVariant item : report["recovery"].as<JsonArray>()) {
            AiRecoveryAction action;
            action.category = item["category"] | "";
            action.detail = item["detail"] | "";
            resp.report.recoveryActions.push_back(action);
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
    std::string reportJson;
    if (nativejson::extractObjectField(content, "report", reportJson)) {
        std::string sourceJson;
        if (nativejson::extractObjectField(reportJson, "source", sourceJson)) {
            nativejson::extractStringField(sourceJson, "type", resp.report.sourceType);
            nativejson::extractStringField(sourceJson, "label", resp.report.sourceLabel);
        }
        std::string analysisJson;
        if (nativejson::extractObjectField(reportJson, "analysis", analysisJson)) {
            nativejson::extractStringField(analysisJson, "normalizedSummary", resp.report.normalizedSummary);
            std::string arrayJson;
            if (nativejson::extractArrayField(analysisJson, "verifiedFacts", arrayJson)) {
                resp.report.verifiedFacts = nativejson::extractStringArrayValues(arrayJson);
            }
            if (nativejson::extractArrayField(analysisJson, "unsupportedClaims", arrayJson)) {
                resp.report.unsupportedClaims = nativejson::extractStringArrayValues(arrayJson);
            }
            if (nativejson::extractArrayField(analysisJson, "biasNotes", arrayJson)) {
                resp.report.biasNotes = nativejson::extractStringArrayValues(arrayJson);
            }
        }
        nativejson::extractStringField(reportJson, "recommendationSummary", resp.report.recommendationSummary);
        nativejson::extractStringField(reportJson, "qcDecision", resp.report.qcDecision);
        std::string scoresJson;
        if (nativejson::extractObjectField(reportJson, "scores", scoresJson)) {
            nativejson::extractUIntField(scoresJson, "quality", resp.report.qualityScore);
            nativejson::extractUIntField(scoresJson, "confidence", resp.report.confidenceScore);
            nativejson::extractUIntField(scoresJson, "contradiction", resp.report.contradictionScore);
            nativejson::extractUIntField(scoresJson, "deception", resp.report.deceptionScore);
        }
        std::string evidenceArray;
        if (nativejson::extractArrayField(reportJson, "evidence", evidenceArray)) {
            for (const std::string& itemJson : nativejson::splitObjectArray(evidenceArray)) {
                AiEvidenceReference ref;
                nativejson::extractStringField(itemJson, "id", ref.id);
                nativejson::extractStringField(itemJson, "type", ref.type);
                nativejson::extractStringField(itemJson, "source", ref.source);
                nativejson::extractStringField(itemJson, "detail", ref.detail);
                resp.report.evidence.push_back(ref);
            }
        }
        std::string reviewersArray;
        if (nativejson::extractArrayField(reportJson, "reviewers", reviewersArray)) {
            for (const std::string& itemJson : nativejson::splitObjectArray(reviewersArray)) {
                AiReviewerOutput reviewer;
                std::string reviewerVerdict;
                nativejson::extractStringField(itemJson, "model", reviewer.model);
                nativejson::extractStringField(itemJson, "verdict", reviewerVerdict);
                reviewer.verdict = _parseVerdict(reviewerVerdict);
                nativejson::extractUIntField(itemJson, "confidence", reviewer.confidence);
                nativejson::extractStringField(itemJson, "notes", reviewer.notes);
                resp.report.reviewers.push_back(reviewer);
            }
        }
        std::string recoveryArray;
        if (nativejson::extractArrayField(reportJson, "recovery", recoveryArray)) {
            for (const std::string& itemJson : nativejson::splitObjectArray(recoveryArray)) {
                AiRecoveryAction action;
                nativejson::extractStringField(itemJson, "category", action.category);
                nativejson::extractStringField(itemJson, "detail", action.detail);
                resp.report.recoveryActions.push_back(action);
            }
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
    if (!resp.report.qcDecision.empty() || !resp.report.evidence.empty() || !resp.report.reviewers.empty()) {
        std::string sanitizeReason;
        if (!_sanitizeReport(resp.report, resp.verdict, sanitizeReason)) {
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

bool AiController::_sanitizeReport(AiReviewReport& report,
                                   AiVerdict verdict,
                                   std::string& reason) const {
    if (report.sourceType.empty() || report.sourceLabel.empty()) {
        reason = "Review report must include source metadata";
        return false;
    }
    if (report.normalizedSummary.empty()) {
        reason = "Review report must include a normalized evidence summary";
        return false;
    }
    if (report.evidence.size() < INNOVATOR_MIN_EVIDENCE_ITEMS) {
        reason = "Review report must include at least one evidence reference";
        return false;
    }
    for (const AiEvidenceReference& ref : report.evidence) {
        if (ref.id.empty() || ref.type.empty() || ref.source.empty() || ref.detail.empty()) {
            reason = "Evidence references must include id, type, source, and detail";
            return false;
        }
    }
    if (report.reviewers.size() < INNOVATOR_MIN_REVIEWERS) {
        reason = "Review report must include at least two reviewer outputs";
        return false;
    }
    bool allPass = true;
    bool anyPass = false;
    bool anyFail = false;
    for (const AiReviewerOutput& reviewer : report.reviewers) {
        if (reviewer.model.empty() || reviewer.notes.empty()) {
            reason = "Reviewer outputs must include model and notes";
            return false;
        }
        if (reviewer.verdict != AiVerdict::PASS && reviewer.verdict != AiVerdict::FAIL) {
            reason = "Reviewer verdicts must be PASS or FAIL";
            return false;
        }
        if (reviewer.confidence > 100U) {
            reason = "Reviewer confidence must be between 0 and 100";
            return false;
        }
        allPass = allPass && reviewer.verdict == AiVerdict::PASS;
        anyPass = anyPass || reviewer.verdict == AiVerdict::PASS;
        anyFail = anyFail || reviewer.verdict == AiVerdict::FAIL;
    }
    if (report.qualityScore > 100U || report.confidenceScore > 100U ||
        report.contradictionScore > 100U || report.deceptionScore > 100U) {
        reason = "Review scores must be between 0 and 100";
        return false;
    }
    if (!isPassLikeDecision(report.qcDecision) &&
        !isRebuildLikeDecision(report.qcDecision) &&
        !isScrapLikeDecision(report.qcDecision)) {
        reason = "Review report qcDecision must be PASS, REBUILD, or SCRAP";
        return false;
    }
    if (report.recommendationSummary.empty()) {
        reason = "Review report must include a recommendation summary";
        return false;
    }
    for (const AiRecoveryAction& action : report.recoveryActions) {
        if (!isAllowedRecoveryCategory(action.category) || action.detail.empty()) {
            reason = "Recovery actions must use an allowlisted category with detail";
            return false;
        }
    }
    if (report.verifiedFacts.empty()) {
        reason = "Review report must include verified facts";
        return false;
    }

    const bool conflictingReviewers = anyPass && anyFail;
    if (isPassLikeDecision(report.qcDecision)) {
        if (report.qualityScore < INNOVATOR_QC_PASS_SCORE ||
            report.confidenceScore < INNOVATOR_QC_PASS_SCORE) {
            reason = "PASS decisions require quality and confidence scores of at least 90";
            return false;
        }
        if (report.contradictionScore > INNOVATOR_QC_MAX_RISK_SCORE ||
            report.deceptionScore > INNOVATOR_QC_MAX_RISK_SCORE) {
            reason = "PASS decisions require low contradiction and deception scores";
            return false;
        }
        if (!report.unsupportedClaims.empty()) {
            reason = "PASS decisions cannot contain unsupported claims";
            return false;
        }
        if (conflictingReviewers || !allPass) {
            reason = "PASS decisions require reviewer consensus";
            return false;
        }
        if (verdict != AiVerdict::PROPOSE && verdict != AiVerdict::PASS) {
            reason = "PASS qcDecision requires a PASS-compatible verdict";
            return false;
        }
    }

    if ((isRebuildLikeDecision(report.qcDecision) || isScrapLikeDecision(report.qcDecision)) &&
        verdict == AiVerdict::PASS) {
        reason = "FAIL review verdict is required for REBUILD or SCRAP decisions";
        return false;
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
