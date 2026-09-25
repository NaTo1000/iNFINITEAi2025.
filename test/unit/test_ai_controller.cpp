#include <unity.h>
#include <string>

#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"

void setUp(void) {}
void tearDown(void) {}

static const char* VALID_PROPOSAL_JSON = R"json(
{"verdict":"PROPOSE","summary":"Collect telemetry",
 "procedure":{"title":"Check link","steps":[{"action":"log","value":"capture wifi"},{"action":"wait_ms","value":"cooldown","valueNumber":250}],"validation":["mqtt remains connected"]},
 "report":{"source":{"type":"mqtt","label":"broker-1"},
 "evidence":[{"id":"ev1","type":"log","source":"device","detail":"wifi reconnect spike"}],
 "analysis":{"normalizedSummary":"Reconnect telemetry was normalized","verifiedFacts":["Reconnect telemetry exceeded baseline"],"unsupportedClaims":[],"biasNotes":["Cloud inference may favor recent failures"]},
 "reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],
 "scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},
 "qcDecision":"PASS",
 "recovery":[{"category":"internal_recovery","detail":"Apply the validated reconnect procedure"}],
 "recommendationSummary":"Proceed with the evidence-backed reconnect procedure"}}}
)json";

void test_parse_procedure_response_accepts_allowlisted_steps() {
    AiResponse resp = AiController::parseStructuredDecisionForTest(
        VALID_PROPOSAL_JSON
    );
    TEST_ASSERT_TRUE(resp.success);
    TEST_ASSERT_EQUAL(static_cast<int>(AiVerdict::PROPOSE), static_cast<int>(resp.verdict));
    TEST_ASSERT_EQUAL_UINT32(2, resp.procedure.steps.size());
    TEST_ASSERT_EQUAL_UINT32(1, resp.report.evidence.size());
    TEST_ASSERT_EQUAL_STRING("PASS", resp.report.qcDecision.c_str());
}

void test_parse_procedure_response_rejects_unknown_actions() {
    AiResponse resp = AiController::parseStructuredDecisionForTest(
        "{\"verdict\":\"PROPOSE\",\"summary\":\"Unsafe\",\"procedure\":{\"title\":\"Hack\",\"steps\":[{\"action\":\"run_cpp\",\"value\":\"bad\"}],\"validation\":[\"none\"]}}"
    );
    TEST_ASSERT_FALSE(resp.success);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resp.reason.find("allowlisted"));
}

void test_evaluate_requires_exact_verdict_field() {
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, R"json(
{"verdict":"FAIL","reason":"Result text mentioned PASS but failed",
 "report":{"source":{"type":"api","label":"review-1"},
 "evidence":[{"id":"ev1","type":"log","source":"device","detail":"Result text conflicted with the baseline"}],
 "analysis":{"normalizedSummary":"Evaluation found unsupported optimism","verifiedFacts":["Log text contradicts the expected state"],"unsupportedClaims":["Claimed success without evidence"],"biasNotes":["Single-model answer favored optimistic wording"]},
 "reviewers":[{"model":"model-a","verdict":"FAIL","confidence":93,"notes":"The evidence does not support PASS"},{"model":"model-b","verdict":"FAIL","confidence":91,"notes":"Consensus is negative"}],
 "scores":{"quality":62,"confidence":58,"contradiction":18,"deception":15},
 "qcDecision":"SCRAP",
 "recovery":[{"category":"no_action","detail":"Discard the unsupported conclusion"}],
 "recommendationSummary":"Scrap the low-quality result"}})json", ""});
    AiController ai(cloud);
    AiResponse resp = ai.evaluate("result mentioned PASS in logs");
    TEST_ASSERT_TRUE(resp.success);
    TEST_ASSERT_EQUAL(static_cast<int>(AiVerdict::FAIL), static_cast<int>(resp.verdict));
    TEST_ASSERT_EQUAL_STRING("SCRAP", resp.report.qcDecision.c_str());
}

void test_malformed_response_fails_cleanly() {
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, "not-json", ""});
    AiController ai(cloud);
    AiResponse resp = ai.query({"ctx", "role"});
    TEST_ASSERT_FALSE(resp.success);
}

void test_missing_evidence_fails_review_report() {
    AiResponse resp = AiController::parseStructuredDecisionForTest(
        R"json({"verdict":"PROPOSE","summary":"Collect telemetry",
        "procedure":{"title":"Check link","steps":[{"action":"log","value":"capture wifi"}],"validation":["mqtt remains connected"]},
        "report":{"source":{"type":"mqtt","label":"broker-1"},
        "evidence":[],
        "analysis":{"normalizedSummary":"Reconnect telemetry was normalized","verifiedFacts":["Reconnect telemetry exceeded baseline"],"unsupportedClaims":[],"biasNotes":[]},
        "reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],
        "scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},
        "qcDecision":"PASS",
        "recovery":[{"category":"internal_recovery","detail":"Apply the validated reconnect procedure"}],
        "recommendationSummary":"Proceed with the evidence-backed reconnect procedure"}})json"
    );
    TEST_ASSERT_FALSE(resp.success);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resp.reason.find("evidence"));
}

void test_conflicting_reviewers_fail_pass_qc() {
    AiResponse resp = AiController::parseStructuredDecisionForTest(
        R"json({"verdict":"PROPOSE","summary":"Collect telemetry",
        "procedure":{"title":"Check link","steps":[{"action":"log","value":"capture wifi"}],"validation":["mqtt remains connected"]},
        "report":{"source":{"type":"mqtt","label":"broker-1"},
        "evidence":[{"id":"ev1","type":"log","source":"device","detail":"wifi reconnect spike"}],
        "analysis":{"normalizedSummary":"Reconnect telemetry was normalized","verifiedFacts":["Reconnect telemetry exceeded baseline"],"unsupportedClaims":[],"biasNotes":["One reviewer trusted a smaller sample"]},
        "reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"FAIL","confidence":94,"notes":"The sample may be incomplete"}],
        "scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},
        "qcDecision":"PASS",
        "recovery":[{"category":"internal_recovery","detail":"Apply the validated reconnect procedure"}],
        "recommendationSummary":"Proceed with the evidence-backed reconnect procedure"}})json"
    );
    TEST_ASSERT_FALSE(resp.success);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resp.reason.find("consensus"));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_parse_procedure_response_accepts_allowlisted_steps);
    RUN_TEST(test_parse_procedure_response_rejects_unknown_actions);
    RUN_TEST(test_evaluate_requires_exact_verdict_field);
    RUN_TEST(test_malformed_response_fails_cleanly);
    RUN_TEST(test_missing_evidence_fails_review_report);
    RUN_TEST(test_conflicting_reviewers_fail_pass_qc);
    return UNITY_END();
}
