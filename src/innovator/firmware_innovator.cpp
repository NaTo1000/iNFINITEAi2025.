// =============================================================================
// firmware_innovator.cpp — Safe innovation loop using allowlisted procedures
// =============================================================================
#include "firmware_innovator.h"
#include "../cloud/cloud_manager.h"
#include "../procedures/procedure_store.h"
#include "sandbox.h"
#include "../common/native_json.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#else
#include <chrono>
#include <cstdio>
#include <stdexcept>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

FirmwareInnovator::FirmwareInnovator(AiController& ai,
                                     CloudManager& cloud,
                                     ProcedureStore& store,
                                     Sandbox& sandbox)
    : _ai(ai), _cloud(cloud), _store(store), _sandbox(sandbox) {}

void FirmwareInnovator::begin() {
    ESP_LOGI(LOG_TAG_INNO, "%s", "Firmware Innovator ready (safe procedure mode)");
}

void FirmwareInnovator::queueTask(const InnovationTask& task) {
    if (_busy) {
        _replacementTask = task;
        _hasReplacement = true;
        _appendLogLine("[" + _currentTask.id + "] cancellation requested by new task " + task.id);
        return;
    }
    _startTask(task);
}

void FirmwareInnovator::tick() {
    if (!_busy || _phase == Phase::IDLE) {
        return;
    }
    if (_hasReplacement) {
        const InnovationTask replacement = _replacementTask;
        _hasReplacement = false;
        _finishTask(false, true, "Task cancelled by replacement request");
        _startTask(replacement);
        return;
    }
    if (_nextPhaseAfterMs != 0U && _nowMs() < _nextPhaseAfterMs) {
        return;
    }
    _nextPhaseAfterMs = 0U;

    switch (_phase) {
        case Phase::DIAGNOSE: {
            _publishLiveStatus("intake", "Normalizing evidence and requesting a structured review");
            _diagnosis = _ai.diagnose(_errorContext);
            if (!_diagnosis.success || _diagnosis.verdict != AiVerdict::PROPOSE) {
                ++_currentResult.iterations;
                if (_currentResult.iterations >= INNOVATOR_MAX_ITERATIONS) {
                    _finishTask(false, false, _diagnosis.reason.empty() ? "AI failed to propose a safe procedure" : _diagnosis.reason);
                    return;
                }
                _errorContext = _diagnosis.reason.empty() ? "AI returned an invalid proposal" : _diagnosis.reason;
                _nextPhaseAfterMs = _nowMs() + INNOVATOR_CYCLE_DELAY_MS;
                return;
            }
            ++_currentResult.iterations;
            _currentResult.reportJson = _serializeReviewReport(_diagnosis.report, _diagnosis.report.qcDecision);
            _phase = Phase::VALIDATE;
            _publishLiveStatus("normalization", "Collected evidence-backed procedure proposal");
            return;
        }
        case Phase::VALIDATE: {
            const std::string serialized = _serializeProcedure(_diagnosis.procedure);
            _currentResult.finalSolution = serialized;
            _publishLiveStatus("quality_control", "Running validation criteria against the proposed review");
            SandboxResult result = _sandbox.run(serialized, [this](const std::string&) {
                const AiProcedurePlan& procedure = _diagnosis.procedure;
                if (procedure.validation.empty()) {
                    throw std::runtime_error("missing validation criteria");
                }
                return _buildValidationSummary(procedure);
            });
            if (!result.passed) {
                if (_currentResult.iterations >= INNOVATOR_MAX_ITERATIONS) {
                    _finishTask(false, false, result.error);
                    return;
                }
                _errorContext = result.error;
                _phase = Phase::DIAGNOSE;
                _nextPhaseAfterMs = _nowMs() + INNOVATOR_CYCLE_DELAY_MS;
                return;
            }
            _sandboxSummary = result.output;
            _phase = Phase::EVALUATE;
            _publishLiveStatus("cross_model_review", "Comparing reviewer outputs and QC thresholds");
            return;
        }
        case Phase::EVALUATE: {
            AiResponse eval = _ai.evaluate(_buildEvaluationContext(_diagnosis, _sandboxSummary));
            if (!eval.report.qcDecision.empty()) {
                _currentResult.reportJson = _serializeReviewReport(eval.report, eval.report.qcDecision);
            }
            _lastReportJson = _currentResult.reportJson;
            _publishLiveStatus("reporting", "Preparing final evidence, analysis, and recovery sections");
            if (eval.success && eval.verdict == AiVerdict::PASS) {
                _currentResult.qcDecision = eval.report.qcDecision.empty() ? "PASS" : eval.report.qcDecision;
                if (!_store.save(_currentTask.description, _currentResult.reportJson, _currentResult.iterations)) {
                    _finishTask(false, false, "Validated procedure could not be persisted");
                    return;
                }
                const std::vector<Procedure> all = _store.loadAll();
                if (!all.empty()) {
                    _currentResult.procedureName = all.back().name;
                }
                _finishTask(true, false, eval.reason.empty() ? eval.decision : eval.reason);
                return;
            }
            _currentResult.qcDecision = eval.report.qcDecision.empty() ? "SCRAP" : eval.report.qcDecision;
            if (eval.report.qcDecision == "SCRAP") {
                _finishTask(false, false, eval.reason.empty() ? "Evidence review scrapped the ingestion" : eval.reason);
                return;
            }
            if (_currentResult.iterations >= INNOVATOR_MAX_ITERATIONS) {
                _finishTask(false, false, eval.reason.empty() ? "Evaluation failed" : eval.reason);
                return;
            }
            _errorContext = eval.reason.empty() ? eval.decision : eval.reason;
            _phase = Phase::DIAGNOSE;
            _nextPhaseAfterMs = _nowMs() + INNOVATOR_CYCLE_DELAY_MS;
            _publishLiveStatus("rebuild", "QC requested a rebuild with stronger evidence");
            return;
        }
        case Phase::IDLE:
            return;
    }
}

void FirmwareInnovator::_startTask(const InnovationTask& task) {
    _currentTask = task;
    _currentResult = {};
    _currentResult.taskId = task.id;
    _errorContext = task.description;
    _lastReportJson.clear();
    _lastStatusSummary.clear();
    _phase = Phase::DIAGNOSE;
    _busy = true;
    _nextPhaseAfterMs = 0U;
    _appendLogLine("[" + task.id + "] queued: " + task.description);
    _publishLiveStatus("intake", "Queued evidence-driven review task");
}

void FirmwareInnovator::_finishTask(bool passed, bool cancelled, const std::string& reason) {
    _currentResult.passed = passed;
    _currentResult.cancelled = cancelled;
    _currentResult.log = reason;
    _lastReportJson = _currentResult.reportJson;
    _appendLogLine("[" + _currentTask.id + "] " + (passed ? "PASS" : cancelled ? "CANCELLED" : "FAIL") + ": " + reason);
    _publishLiveStatus(cancelled ? "cancelled" : passed ? "passed" : "failed", reason);
    _publishResult(_currentResult);
    if (_completeCb) {
        _completeCb(_currentResult);
    }
    _phase = Phase::IDLE;
    _busy = false;
}

void FirmwareInnovator::_appendLogLine(const std::string& line) {
    _log += line + "\n";
    if (_log.size() > INNOVATOR_MAX_LOG_BYTES) {
        _log.erase(0, _log.size() - INNOVATOR_MAX_LOG_BYTES);
    }
}

void FirmwareInnovator::_publishResult(const InnovationResult& result) {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["task"] = result.taskId.c_str();
    doc["passed"] = result.passed;
    doc["cancelled"] = result.cancelled;
    doc["iterations"] = result.iterations;
    doc["procedure"] = result.procedureName.c_str();
    doc["log"] = result.log.c_str();
    doc["qcDecision"] = result.qcDecision.c_str();
    doc["stage"] = currentStage().c_str();
    if (!result.reportJson.empty()) {
        JsonDocument reportDoc;
        if (!deserializeJson(reportDoc, result.reportJson.c_str())) {
            doc["report"] = reportDoc.as<JsonObject>();
        }
    }
    std::string json;
    serializeJson(doc, json);
    _cloud.publishInnovatorResult(json);
#else
    ESP_LOGI(LOG_TAG_INNO, "task=%s passed=%s cancelled=%s iterations=%u",
             result.taskId.c_str(),
             result.passed ? "true" : "false",
             result.cancelled ? "true" : "false",
             result.iterations);
#endif
}

void FirmwareInnovator::_publishLiveStatus(const std::string& state, const std::string& summary) {
    _lastStatusSummary = summary;
    _appendLogLine("[" + _currentTask.id + "] " + state + ": " + summary);
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["task"] = _currentTask.id.c_str();
    doc["busy"] = _busy;
    doc["stage"] = currentStage().c_str();
    doc["state"] = state.c_str();
    doc["iterations"] = _currentResult.iterations;
    doc["summary"] = summary.c_str();
    if (!_currentResult.qcDecision.empty()) {
        doc["qcDecision"] = _currentResult.qcDecision.c_str();
    }
    std::string json;
    serializeJson(doc, json);
    _cloud.publishStatus(json);
#endif
}

std::string FirmwareInnovator::_serializeProcedure(const AiProcedurePlan& procedure) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["title"] = procedure.title.c_str();
    doc["summary"] = procedure.summary.c_str();
    JsonArray steps = doc["steps"].to<JsonArray>();
    for (const AiProcedureStep& step : procedure.steps) {
        JsonObject obj = steps.add<JsonObject>();
        obj["action"] = step.action.c_str();
        obj["value"] = step.value.c_str();
        obj["valueNumber"] = step.valueNumber;
    }
    JsonArray validation = doc["validation"].to<JsonArray>();
    for (const std::string& rule : procedure.validation) {
        validation.add(rule.c_str());
    }
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    std::string out = "{\"title\":\"" + nativejson::escapeString(procedure.title) +
                      "\",\"summary\":\"" + nativejson::escapeString(procedure.summary) +
                      "\",\"steps\":[";
    for (size_t i = 0; i < procedure.steps.size(); ++i) {
        if (i) out += ',';
        out += "{\"action\":\"" + nativejson::escapeString(procedure.steps[i].action) +
               "\",\"value\":\"" + nativejson::escapeString(procedure.steps[i].value) +
               "\",\"valueNumber\":" + std::to_string(procedure.steps[i].valueNumber) + "}";
    }
    out += "],\"validation\":[";
    for (size_t i = 0; i < procedure.validation.size(); ++i) {
        if (i) out += ',';
        out += '"' + nativejson::escapeString(procedure.validation[i]) + '"';
    }
    out += "]}";
    return out;
#endif
}

std::string FirmwareInnovator::_serializeReviewReport(const AiReviewReport& report,
                                                      const std::string& finalDecision) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    JsonObject source = doc["source"].to<JsonObject>();
    source["type"] = report.sourceType.c_str();
    source["label"] = report.sourceLabel.c_str();
    JsonArray evidence = doc["evidence"].to<JsonArray>();
    for (const AiEvidenceReference& ref : report.evidence) {
        JsonObject item = evidence.add<JsonObject>();
        item["id"] = ref.id.c_str();
        item["type"] = ref.type.c_str();
        item["source"] = ref.source.c_str();
        item["detail"] = ref.detail.c_str();
    }
    JsonObject analysis = doc["analysis"].to<JsonObject>();
    analysis["normalizedSummary"] = report.normalizedSummary.c_str();
    JsonArray verifiedFacts = analysis["verifiedFacts"].to<JsonArray>();
    for (const std::string& fact : report.verifiedFacts) {
        verifiedFacts.add(fact.c_str());
    }
    JsonArray unsupportedClaims = analysis["unsupportedClaims"].to<JsonArray>();
    for (const std::string& claim : report.unsupportedClaims) {
        unsupportedClaims.add(claim.c_str());
    }
    JsonArray biasNotes = analysis["biasNotes"].to<JsonArray>();
    for (const std::string& note : report.biasNotes) {
        biasNotes.add(note.c_str());
    }
    JsonArray reviewers = doc["reviewers"].to<JsonArray>();
    for (const AiReviewerOutput& reviewer : report.reviewers) {
        JsonObject item = reviewers.add<JsonObject>();
        item["model"] = reviewer.model.c_str();
        item["verdict"] = reviewer.verdict == AiVerdict::PASS ? "PASS" :
            reviewer.verdict == AiVerdict::FAIL ? "FAIL" : "NONE";
        item["confidence"] = reviewer.confidence;
        item["notes"] = reviewer.notes.c_str();
    }
    JsonObject scores = doc["scores"].to<JsonObject>();
    scores["quality"] = report.qualityScore;
    scores["confidence"] = report.confidenceScore;
    scores["contradiction"] = report.contradictionScore;
    scores["deception"] = report.deceptionScore;
    doc["qcDecision"] = finalDecision.c_str();
    JsonArray recovery = doc["recommendedRecoveryActions"].to<JsonArray>();
    for (const AiRecoveryAction& action : report.recoveryActions) {
        JsonObject item = recovery.add<JsonObject>();
        item["category"] = action.category.c_str();
        item["detail"] = action.detail.c_str();
    }
    doc["recommendationSummary"] = report.recommendationSummary.c_str();
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    std::string out = "{\"source\":{\"type\":\"" + nativejson::escapeString(report.sourceType) +
                      "\",\"label\":\"" + nativejson::escapeString(report.sourceLabel) +
                      "\"},\"evidence\":[";
    for (size_t i = 0; i < report.evidence.size(); ++i) {
        if (i) out += ',';
        const AiEvidenceReference& ref = report.evidence[i];
        out += "{\"id\":\"" + nativejson::escapeString(ref.id) +
               "\",\"type\":\"" + nativejson::escapeString(ref.type) +
               "\",\"source\":\"" + nativejson::escapeString(ref.source) +
               "\",\"detail\":\"" + nativejson::escapeString(ref.detail) + "\"}";
    }
    out += "],\"analysis\":{\"normalizedSummary\":\"" + nativejson::escapeString(report.normalizedSummary) +
           "\",\"verifiedFacts\":[";
    for (size_t i = 0; i < report.verifiedFacts.size(); ++i) {
        if (i) out += ',';
        out += '"' + nativejson::escapeString(report.verifiedFacts[i]) + '"';
    }
    out += "],\"unsupportedClaims\":[";
    for (size_t i = 0; i < report.unsupportedClaims.size(); ++i) {
        if (i) out += ',';
        out += '"' + nativejson::escapeString(report.unsupportedClaims[i]) + '"';
    }
    out += "],\"biasNotes\":[";
    for (size_t i = 0; i < report.biasNotes.size(); ++i) {
        if (i) out += ',';
        out += '"' + nativejson::escapeString(report.biasNotes[i]) + '"';
    }
    out += "]},\"reviewers\":[";
    for (size_t i = 0; i < report.reviewers.size(); ++i) {
        if (i) out += ',';
        const AiReviewerOutput& reviewer = report.reviewers[i];
        out += "{\"model\":\"" + nativejson::escapeString(reviewer.model) +
               "\",\"verdict\":\"" + std::string(reviewer.verdict == AiVerdict::PASS ? "PASS" :
                                                  reviewer.verdict == AiVerdict::FAIL ? "FAIL" : "NONE") +
               "\",\"confidence\":" + std::to_string(reviewer.confidence) +
               ",\"notes\":\"" + nativejson::escapeString(reviewer.notes) + "\"}";
    }
    out += "],\"scores\":{\"quality\":" + std::to_string(report.qualityScore) +
           ",\"confidence\":" + std::to_string(report.confidenceScore) +
           ",\"contradiction\":" + std::to_string(report.contradictionScore) +
           ",\"deception\":" + std::to_string(report.deceptionScore) +
           "},\"qcDecision\":\"" + nativejson::escapeString(finalDecision) +
           "\",\"recommendedRecoveryActions\":[";
    for (size_t i = 0; i < report.recoveryActions.size(); ++i) {
        if (i) out += ',';
        const AiRecoveryAction& action = report.recoveryActions[i];
        out += "{\"category\":\"" + nativejson::escapeString(action.category) +
               "\",\"detail\":\"" + nativejson::escapeString(action.detail) + "\"}";
    }
    out += "],\"recommendationSummary\":\"" + nativejson::escapeString(report.recommendationSummary) + "\"}";
    return out;
#endif
}

std::string FirmwareInnovator::_buildValidationSummary(const AiProcedurePlan& procedure) const {
    std::string summary = "Validated safe procedure '" + procedure.title + "' with steps:";
    for (const AiProcedureStep& step : procedure.steps) {
        summary += " [" + step.action + "]";
    }
    summary += " Criteria:";
    for (const std::string& rule : procedure.validation) {
        summary += " " + rule + ";";
    }
    return summary;
}

std::string FirmwareInnovator::_buildEvaluationContext(const AiResponse& diagnosis,
                                                       const std::string& validationSummary) const {
    return std::string("Task: ") + _currentTask.description +
           "\nProcedure: " + _serializeProcedure(diagnosis.procedure) +
           "\nReport: " + _serializeReviewReport(diagnosis.report, diagnosis.report.qcDecision) +
           "\nValidation: " + validationSummary;
}

std::string FirmwareInnovator::currentStage() const {
    switch (_phase) {
        case Phase::DIAGNOSE: return "intake";
        case Phase::VALIDATE: return "quality_control";
        case Phase::EVALUATE: return "reporting";
        case Phase::IDLE: return "idle";
    }
    return "idle";
}

uint32_t FirmwareInnovator::_nowMs() const {
#ifndef NATIVE_TEST
    return static_cast<uint32_t>(millis());
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFFU);
#endif
}
