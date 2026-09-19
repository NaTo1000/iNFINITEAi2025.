#pragma once
// =============================================================================
// ai_controller.h — Cloud AI control module
// =============================================================================
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../config.h"

class CloudManager;

using AiDecisionCallback = std::function<void(const std::string& decision)>;

enum class AiVerdict : uint8_t {
    NONE = 0,
    PROPOSE,
    PASS,
    FAIL,
    ERROR,
};

struct AiProcedureStep {
    std::string action;
    std::string value;
    uint32_t    valueNumber = 0;
};

struct AiProcedurePlan {
    std::string title;
    std::string summary;
    std::vector<AiProcedureStep> steps;
    std::vector<std::string> validation;
};

struct AiRequest {
    std::string context;
    std::string systemRole;
};

struct AiResponse {
    bool            success  = false;
    AiVerdict       verdict  = AiVerdict::NONE;
    std::string     decision;
    std::string     reason;
    std::string     raw;
    AiProcedurePlan procedure;
};

class AiController {
public:
    explicit AiController(CloudManager& cloud);

    AiResponse query(const AiRequest& req);
    void onDecision(AiDecisionCallback cb) { _decisionCb = cb; }
    AiResponse diagnose(const std::string& errorDescription);
    AiResponse evaluate(const std::string& testResult);

#ifdef NATIVE_TEST
    static AiResponse parseStructuredDecisionForTest(const std::string& content);
#endif

private:
    std::string _buildPayload(const AiRequest& req) const;
    std::string _extractContent(const std::string& rawJson) const;
    AiResponse  _parseStructuredDecision(const std::string& content) const;
    bool        _sanitizeProcedure(AiProcedurePlan& procedure, std::string& reason) const;
    static AiVerdict _parseVerdict(const std::string& verdictText);

    CloudManager&       _cloud;
    AiDecisionCallback  _decisionCb;
};
