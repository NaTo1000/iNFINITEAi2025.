#pragma once
// =============================================================================
// sandbox.h — Sandboxed execution environment for firmware innovation
//
// The sandbox is a lightweight runtime guard that:
//  • Caps heap allocation to INNOVATOR_SANDBOX_HEAP_LIMIT
//  • Catches exceptions / hard-faults and reports them safely
//  • Executes a "test script" (a small callable) in isolation
//  • Returns a SandboxResult with pass/fail and captured output
// =============================================================================
#include <string>
#include <functional>
#include <cstdint>
#include "../config.h"

// A test callable: receives a context JSON string, returns a result string
using SandboxTest = std::function<std::string(const std::string& context)>;

struct SandboxResult {
    bool        passed   = false;
    std::string output;       // test output / captured log
    std::string error;        // error message (empty on pass)
    uint32_t    heapBefore;   // free heap before test (bytes)
    uint32_t    heapAfter;    // free heap after test (bytes)
    uint32_t    durationMs;   // wall-clock execution time
};

class Sandbox {
public:
    Sandbox();

    // Run a test inside the sandbox
    SandboxResult run(const std::string& context, SandboxTest test);

    // Check whether the system has enough headroom to run a test
    bool hasHeadroom() const;

    // Last recorded free heap
    uint32_t freeHeap() const;

private:
    uint32_t _getHeap() const;
    uint32_t _getTimeMs() const;
};
