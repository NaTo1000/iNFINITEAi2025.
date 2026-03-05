// =============================================================================
// sandbox.cpp — Sandboxed execution environment
// =============================================================================
#include "sandbox.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <esp_log.h>
#else
#include <cstdio>
#include <cstdlib>
#include <chrono>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

Sandbox::Sandbox() {}

// -----------------------------------------------------------------------------
bool Sandbox::hasHeadroom() const {
    return _getHeap() > INNOVATOR_SANDBOX_HEAP_LIMIT;
}

uint32_t Sandbox::freeHeap() const { return _getHeap(); }

// -----------------------------------------------------------------------------
SandboxResult Sandbox::run(const std::string& context, SandboxTest test) {
    SandboxResult result;
    result.heapBefore = _getHeap();

    if (!hasHeadroom()) {
        result.passed = false;
        result.error  = "Insufficient heap headroom for sandbox execution";
        ESP_LOGW(LOG_TAG_SANDBOX, "%s", result.error.c_str());
        return result;
    }

    uint32_t t0 = _getTimeMs();
    try {
        result.output = test(context);
        result.passed = true;
        ESP_LOGI(LOG_TAG_SANDBOX, "Test PASSED — output: %s",
                 result.output.c_str());
    } catch (const std::exception& ex) {
        result.passed = false;
        result.error  = std::string("Exception: ") + ex.what();
        ESP_LOGE(LOG_TAG_SANDBOX, "%s", result.error.c_str());
    } catch (...) {
        result.passed = false;
        result.error  = "Unknown exception in sandbox";
        ESP_LOGE(LOG_TAG_SANDBOX, "%s", result.error.c_str());
    }

    result.heapAfter   = _getHeap();
    result.durationMs  = _getTimeMs() - t0;
    return result;
}

// -----------------------------------------------------------------------------
// Platform helpers
// -----------------------------------------------------------------------------
uint32_t Sandbox::_getHeap() const {
#ifndef NATIVE_TEST
    return static_cast<uint32_t>(ESP.getFreeHeap());
#else
    // Simulate 80 KB free heap in unit tests
    return 80 * 1024;
#endif
}

uint32_t Sandbox::_getTimeMs() const {
#ifndef NATIVE_TEST
    return static_cast<uint32_t>(millis());
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);
#endif
}
