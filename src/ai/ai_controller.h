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

struct AiEvidenceReference {
    std::string id;
    std::string type;
    std::string source;
    std::string detail;
};

struct AiReviewerOutput {
    std::string model;
    AiVerdict   verdict = AiVerdict::NONE;
    uint32_t    confidence = 0;
    std::string notes;
};

struct AiRecoveryAction {
    std::string category;
    std::string detail;
};

struct AiReviewReport {
    std::string sourceType;
    std::string sourceLabel;
    std::string normalizedSummary;
    uint32_t    qualityScore = 0;
    uint32_t    confidenceScore = 0;
    uint32_t    contradictionScore = 0;
    uint32_t    deceptionScore = 0;
    std::string qcDecision;
    std::string recommendationSummary;
    std::vector<AiEvidenceReference> evidence;
    std::vector<std::string> verifiedFacts;
    std::vector<std::string> unsupportedClaims;
    std::vector<std::string> biasNotes;
    std::vector<AiReviewerOutput> reviewers;
    std::vector<AiRecoveryAction> recoveryActions;
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
    AiReviewReport  report;
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
    bool        _sanitizeReport(AiReviewReport& report, AiVerdict verdict, std::string& reason) const;
    static AiVerdict _parseVerdict(const std::string& verdictText);

    CloudManager&       _cloud;
    AiDecisionCallback  _decisionCb;
};
