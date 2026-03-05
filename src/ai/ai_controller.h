#pragma once
// =============================================================================
// ai_controller.h — On-board AI control module
//
// Responsibilities:
//  • Build prompts from sensor/error context
//  • Call the cloud LLM via CloudManager::httpPost
//  • Parse the JSON response and extract an action or solution
//  • Notify subscribers of the AI decision
// =============================================================================
#include <string>
#include <functional>
#include "../config.h"

// Forward declaration to avoid circular dependency
class CloudManager;

// Callback fired when the AI produces a decision/action string
using AiDecisionCallback = std::function<void(const std::string& decision)>;

struct AiRequest {
    std::string context;    // e.g. error message or task description
    std::string systemRole; // system prompt
};

struct AiResponse {
    bool        success  = false;
    std::string decision;   // extracted action text
    std::string raw;        // full raw JSON from API
};

class AiController {
public:
    explicit AiController(CloudManager& cloud);

    // Synchronous query — blocks until HTTP response (use in task context)
    AiResponse query(const AiRequest& req);

    // Register callback for decisions
    void onDecision(AiDecisionCallback cb) { _decisionCb = cb; }

    // Convenience: ask AI to diagnose an error and suggest a fix
    AiResponse diagnose(const std::string& errorDescription);

    // Convenience: ask AI to evaluate an innovation result
    AiResponse evaluate(const std::string& testResult);

private:
    std::string _buildPayload(const AiRequest& req) const;
    std::string _extractContent(const std::string& rawJson) const;

    CloudManager&    _cloud;
    AiDecisionCallback _decisionCb;
};
