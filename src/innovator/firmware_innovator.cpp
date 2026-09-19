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
            _phase = Phase::VALIDATE;
            return;
        }
        case Phase::VALIDATE: {
            const std::string serialized = _serializeProcedure(_diagnosis.procedure);
            _currentResult.finalSolution = serialized;
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
            return;
        }
        case Phase::EVALUATE: {
            AiResponse eval = _ai.evaluate(_sandboxSummary);
            if (eval.success && eval.verdict == AiVerdict::PASS) {
                if (!_store.save(_currentTask.description, _currentResult.finalSolution, _currentResult.iterations)) {
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
            if (_currentResult.iterations >= INNOVATOR_MAX_ITERATIONS) {
                _finishTask(false, false, eval.reason.empty() ? "Evaluation failed" : eval.reason);
                return;
            }
            _errorContext = eval.reason.empty() ? eval.decision : eval.reason;
            _phase = Phase::DIAGNOSE;
            _nextPhaseAfterMs = _nowMs() + INNOVATOR_CYCLE_DELAY_MS;
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
    _phase = Phase::DIAGNOSE;
    _busy = true;
    _nextPhaseAfterMs = 0U;
    _appendLogLine("[" + task.id + "] queued: " + task.description);
}

void FirmwareInnovator::_finishTask(bool passed, bool cancelled, const std::string& reason) {
    _currentResult.passed = passed;
    _currentResult.cancelled = cancelled;
    _currentResult.log = reason;
    _appendLogLine("[" + _currentTask.id + "] " + (passed ? "PASS" : cancelled ? "CANCELLED" : "FAIL") + ": " + reason);
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

uint32_t FirmwareInnovator::_nowMs() const {
#ifndef NATIVE_TEST
    return static_cast<uint32_t>(millis());
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFFU);
#endif
}
