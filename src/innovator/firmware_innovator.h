#pragma once
// =============================================================================
// firmware_innovator.h — Safe, bounded procedure generation loop
// =============================================================================
#include <cstdint>
#include <functional>
#include <string>
#include "../config.h"
#include "../ai/ai_controller.h"

class CloudManager;
class ProcedureStore;
class Sandbox;

struct InnovationTask {
    std::string id;
    std::string description;
    std::string context;
};

struct InnovationResult {
    std::string taskId;
    bool        passed        = false;
    bool        cancelled     = false;
    uint32_t    iterations    = 0;
    std::string procedureName;
    std::string finalSolution;
    std::string log;
    std::string reportJson;
    std::string qcDecision;
};

using InnovationCompleteCallback = std::function<void(const InnovationResult& result)>;

class FirmwareInnovator {
public:
    FirmwareInnovator(AiController& ai,
                      CloudManager& cloud,
                      ProcedureStore& store,
                      Sandbox& sandbox);

    void begin();
    void queueTask(const InnovationTask& task);
    void tick();
    void onComplete(InnovationCompleteCallback cb) { _completeCb = cb; }
    bool isBusy() const { return _busy; }
    const std::string& getLog() const { return _log; }
    std::string currentStage() const;
    std::string currentTaskId() const { return _currentTask.id; }
    uint32_t currentIterations() const { return _currentResult.iterations; }
    const std::string& lastReportJson() const { return _lastReportJson; }
    const std::string& lastStatusSummary() const { return _lastStatusSummary; }

private:
    enum class Phase : uint8_t {
        IDLE = 0,
        DIAGNOSE,
        VALIDATE,
        EVALUATE,
    };

    void _startTask(const InnovationTask& task);
    void _finishTask(bool passed, bool cancelled, const std::string& reason);
    void _appendLogLine(const std::string& line);
    void _publishResult(const InnovationResult& result);
    void _publishLiveStatus(const std::string& state, const std::string& summary);
    std::string _serializeProcedure(const AiProcedurePlan& procedure) const;
    std::string _serializeReviewReport(const AiReviewReport& report,
                                       const std::string& finalDecision) const;
    std::string _buildValidationSummary(const AiProcedurePlan& procedure) const;
    std::string _buildEvaluationContext(const AiResponse& diagnosis,
                                        const std::string& validationSummary) const;
    uint32_t _nowMs() const;

    AiController&   _ai;
    CloudManager&   _cloud;
    ProcedureStore& _store;
    Sandbox&        _sandbox;

    InnovationCompleteCallback _completeCb;
    InnovationTask    _currentTask;
    InnovationTask    _replacementTask;
    InnovationResult  _currentResult;
    AiResponse        _diagnosis;
    std::string       _errorContext;
    std::string       _sandboxSummary;
    std::string       _lastReportJson;
    std::string       _lastStatusSummary;
    std::string       _log;
    Phase             _phase = Phase::IDLE;
    bool              _busy = false;
    bool              _hasReplacement = false;
    uint32_t          _nextPhaseAfterMs = 0;
};
