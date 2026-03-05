// =============================================================================
// firmware_innovator.cpp — Self-healing / self-improving innovation loop
// =============================================================================
#include "firmware_innovator.h"
#include "../ai/ai_controller.h"
#include "../cloud/cloud_manager.h"
#include "../procedures/procedure_store.h"
#include "sandbox.h"

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

FirmwareInnovator::FirmwareInnovator(AiController&   ai,
                                     CloudManager&   cloud,
                                     ProcedureStore& store,
                                     Sandbox&        sandbox)
    : _ai(ai), _cloud(cloud), _store(store), _sandbox(sandbox) {}

// -----------------------------------------------------------------------------
void FirmwareInnovator::begin() {
    ESP_LOGI(LOG_TAG_INNO, "Firmware Innovator ready (max_iter=%d)",
             INNOVATOR_MAX_ITERATIONS);
}

// -----------------------------------------------------------------------------
void FirmwareInnovator::queueTask(const InnovationTask& task) {
    if (_busy) {
        ESP_LOGW(LOG_TAG_INNO, "Already busy — task '%s' will replace queue",
                 task.id.c_str());
    }
    _currentTask  = task;
    _taskPending  = true;
    ESP_LOGI(LOG_TAG_INNO, "Task queued: [%s] %s",
             task.id.c_str(), task.description.c_str());
}

// -----------------------------------------------------------------------------
void FirmwareInnovator::tick() {
    if (!_taskPending || _busy) return;
    _taskPending = false;
    _busy        = true;

    InnovationResult result;
    result.taskId = _currentTask.id;

    ESP_LOGI(LOG_TAG_INNO, "▶ Starting innovation cycle for task [%s]",
             _currentTask.id.c_str());

    bool success = _runCycle(_currentTask, result);

    if (success) {
        // Save with a funny name: description = original task, code = solution
        _store.save(_currentTask.description,
                    result.finalSolution,
                    result.iterations);
        // Pick up the last-saved name
        auto all = _store.loadAll();
        if (!all.empty()) result.procedureName = all.back().name;

        _log += "[" + result.taskId + "] ✓ → " + result.procedureName + "\n";
        ESP_LOGI(LOG_TAG_INNO,
                 "✓ Innovation complete! Procedure saved as '%s' after %u iter",
                 result.procedureName.c_str(), result.iterations);
    } else {
        _log += "[" + result.taskId + "] ✗ exhausted " +
                std::to_string(INNOVATOR_MAX_ITERATIONS) + " iterations\n";
        ESP_LOGW(LOG_TAG_INNO, "✗ Innovation failed after %u iterations",
                 result.iterations);
    }

    result.passed = success;
    _publishResult(result);
    if (_completeCb) _completeCb(result);
    _busy = false;
}

// -----------------------------------------------------------------------------
// Core innovation cycle: iterate AI → sandbox → evaluate until PASS or limit
// -----------------------------------------------------------------------------
bool FirmwareInnovator::_runCycle(const InnovationTask& task,
                                   InnovationResult&     result) {
    std::string errorContext = task.description;

    for (uint32_t iter = 1; iter <= INNOVATOR_MAX_ITERATIONS; ++iter) {
        result.iterations = iter;
        ESP_LOGI(LOG_TAG_INNO, "  Iteration %u/%d — diagnosing …",
                 iter, INNOVATOR_MAX_ITERATIONS);

        // Step 1: ask AI for a solution
        AiResponse aiResp = _ai.diagnose(errorContext);
        if (!aiResp.success) {
            ESP_LOGW(LOG_TAG_INNO, "  AI diagnosis failed at iter %u", iter);
            errorContext = "AI returned no solution. Previous: " + errorContext;
            continue;
        }
        result.finalSolution = aiResp.decision;

        // Step 2: run in sandbox
        std::string sandboxCtx = task.context.empty()
            ? "{\"solution\":\"" + aiResp.decision + "\"}"
            : task.context;

        std::string solutionCode = aiResp.decision; // capture for lambda
        SandboxResult sboxRes = _sandbox.run(sandboxCtx,
            [&solutionCode](const std::string& ctx) -> std::string {
                // In a real device this would dynamically patch a config or
                // run a compiled snippet via a scripting VM.
                // Here we simulate by checking the solution is non-trivial.
                if (solutionCode.size() < INNOVATOR_MIN_SOLUTION_LENGTH) {
                    throw std::runtime_error("solution too short — not viable");
                }
                return "Sandbox executed solution (" +
                       std::to_string(solutionCode.size()) + " chars). OK.";
            });

        if (!sboxRes.passed) {
            ESP_LOGW(LOG_TAG_INNO, "  Sandbox FAIL: %s", sboxRes.error.c_str());
            errorContext = "Sandbox error: " + sboxRes.error;
            continue;
        }

        // Step 3: ask AI to evaluate the result
        AiResponse evalResp = _ai.evaluate(sboxRes.output);
        if (evalResp.success &&
            evalResp.decision.find("PASS") != std::string::npos) {
            ESP_LOGI(LOG_TAG_INNO,
                     "  AI evaluation: PASS at iteration %u", iter);
            return true;  // 🎉 peak standards reached
        }

        // Not PASS — feed evaluation back as error context
        errorContext = "Previous solution failed evaluation: " +
                       evalResp.decision + ". Solution was: " + aiResp.decision;
        ESP_LOGI(LOG_TAG_INNO, "  AI evaluation: not PASS — iterating");

#ifndef NATIVE_TEST
        delay(INNOVATOR_CYCLE_DELAY_MS);
#endif
    }
    return false; // exhausted iterations
}

// -----------------------------------------------------------------------------
void FirmwareInnovator::_publishResult(const InnovationResult& result) {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["task"]       = result.taskId.c_str();
    doc["passed"]     = result.passed;
    doc["iterations"] = result.iterations;
    doc["procedure"]  = result.procedureName.c_str();
    std::string json;
    serializeJson(doc, json);
    _cloud.publishInnovatorResult(json);
#else
    ESP_LOGI(LOG_TAG_INNO, "Result: task=%s passed=%s iters=%u proc=%s",
             result.taskId.c_str(),
             result.passed ? "true" : "false",
             result.iterations,
             result.procedureName.c_str());
#endif
}
