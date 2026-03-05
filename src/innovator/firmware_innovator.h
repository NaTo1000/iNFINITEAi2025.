#pragma once
// =============================================================================
// firmware_innovator.h — On-board self-healing and self-improving engine
//
// Workflow per innovation cycle:
//  1. Receive an error report (from watchdog, MQTT, or mobile API)
//  2. Call AiController::diagnose() to get a proposed solution
//  3. Run the solution in the Sandbox
//  4. Call AiController::evaluate() on the sandbox output
//  5. If AI says PASS  → save procedure via ProcedureStore (funny name)
//     If AI says FAIL  → iterate (max INNOVATOR_MAX_ITERATIONS)
//  6. Publish result to MQTT and notify mobile via BLE
//  7. Start the next innovation task automatically
// =============================================================================
#include <string>
#include <functional>
#include <cstdint>
#include "../config.h"

// Forward declarations
class AiController;
class CloudManager;
class ProcedureStore;
class Sandbox;

// A task to be innovated: a description of what went wrong / what to improve
struct InnovationTask {
    std::string id;           // unique task ID
    std::string description;  // error or improvement description
    std::string context;      // additional JSON context
};

// Innovation cycle result
struct InnovationResult {
    std::string taskId;
    bool        passed        = false;
    uint32_t    iterations    = 0;
    std::string procedureName; // funny name (only set on success)
    std::string finalSolution;
    std::string log;           // human-readable cycle log
};

using InnovationCompleteCallback =
    std::function<void(const InnovationResult& result)>;

class FirmwareInnovator {
public:
    FirmwareInnovator(AiController&   ai,
                      CloudManager&   cloud,
                      ProcedureStore& store,
                      Sandbox&        sandbox);

    void begin();

    // Queue a new innovation task (can be called from any context)
    void queueTask(const InnovationTask& task);

    // Process one pending task (call from main loop — non-blocking iteration)
    void tick();

    // Register completion callback
    void onComplete(InnovationCompleteCallback cb) { _completeCb = cb; }

    // Returns true if the innovator is currently running a task
    bool isBusy() const { return _busy; }

    // Returns log of all completed innovations
    const std::string& getLog() const { return _log; }

private:
    bool    _runCycle(const InnovationTask& task, InnovationResult& result);
    void    _publishResult(const InnovationResult& result);
    std::string _buildSandboxTest(const std::string& aiSolution) const;

    AiController&   _ai;
    CloudManager&   _cloud;
    ProcedureStore& _store;
    Sandbox&        _sandbox;

    InnovationCompleteCallback _completeCb;

    InnovationTask    _currentTask;
    bool              _busy          = false;
    bool              _taskPending   = false;
    std::string       _log;
    uint32_t          _taskCounter   = 0;
};
