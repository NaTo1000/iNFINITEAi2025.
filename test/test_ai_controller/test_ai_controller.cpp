#include <unity.h>
#include <string>

#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_procedure_response_accepts_allowlisted_steps() {
    AiResponse resp = AiController::parseStructuredDecisionForTest(
        "{\"verdict\":\"PROPOSE\",\"summary\":\"Collect telemetry\"," \
        "\"procedure\":{\"title\":\"Check link\",\"steps\":[{\"action\":\"log\",\"value\":\"capture wifi\"},{\"action\":\"wait_ms\",\"value\":\"cooldown\",\"valueNumber\":250}],\"validation\":[\"mqtt remains connected\"]}}"
    );
    TEST_ASSERT_TRUE(resp.success);
    TEST_ASSERT_EQUAL(static_cast<int>(AiVerdict::PROPOSE), static_cast<int>(resp.verdict));
    TEST_ASSERT_EQUAL_UINT32(2, resp.procedure.steps.size());
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
    cloud.queueMockHttpResponse({true, 200, 1, "{\"verdict\":\"FAIL\",\"reason\":\"Result text mentioned PASS but failed\"}", ""});
    AiController ai(cloud);
    AiResponse resp = ai.evaluate("result mentioned PASS in logs");
    TEST_ASSERT_TRUE(resp.success);
    TEST_ASSERT_EQUAL(static_cast<int>(AiVerdict::FAIL), static_cast<int>(resp.verdict));
}

void test_malformed_response_fails_cleanly() {
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, "not-json", ""});
    AiController ai(cloud);
    AiResponse resp = ai.query({"ctx", "role"});
    TEST_ASSERT_FALSE(resp.success);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_parse_procedure_response_accepts_allowlisted_steps);
    RUN_TEST(test_parse_procedure_response_rejects_unknown_actions);
    RUN_TEST(test_evaluate_requires_exact_verdict_field);
    RUN_TEST(test_malformed_response_fails_cleanly);
    return UNITY_END();
}
