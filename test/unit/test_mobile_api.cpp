#include <unity.h>
#include <string>

#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"
#include "flipper/flipper_bridge.h"
#include "innovator/sandbox.h"
#include "innovator/firmware_innovator.h"
#include "mobile/mobile_api.h"
#include "procedures/procedure_store.h"
#include "radio/radio_measurement.h"

void setUp(void) {}
void tearDown(void) {}

void test_authentication_requires_bearer_token() {
    CloudManager cloud;
    AiController ai(cloud);
    FlipperBridge flipper;
    ProcedureStore store("/tmp/infiniteai-mobile-auth");
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    RadioMeasurement measurement;
    MobileApi api(cloud, ai, flipper, store, innovator, measurement);
    const std::string valid = std::string("Bearer ") + API_AUTH_TOKEN;
    TEST_ASSERT_TRUE(api.authenticateForTest(valid));
    TEST_ASSERT_FALSE(api.authenticateForTest("******"));
}

void test_fragmented_body_reassembles_in_order() {
    CloudManager cloud;
    AiController ai(cloud);
    FlipperBridge flipper;
    ProcedureStore store("/tmp/infiniteai-mobile-frag");
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    RadioMeasurement measurement;
    MobileApi api(cloud, ai, flipper, store, innovator, measurement);

    const std::string body = "{\"action\":\"ping\"}";
    auto first = api.accumulateBodyChunkForTest("req1", body.substr(0, 10), 0, body.size());
    TEST_ASSERT_TRUE(first.accepted);
    TEST_ASSERT_FALSE(first.complete);
    auto second = api.accumulateBodyChunkForTest("req1", body.substr(10), 10, body.size());
    TEST_ASSERT_TRUE(second.accepted);
    TEST_ASSERT_TRUE(second.complete);
    TEST_ASSERT_EQUAL_STRING(body.c_str(), second.body.c_str());
}

void test_fragmented_body_rejects_invalid_sequence() {
    CloudManager cloud;
    AiController ai(cloud);
    FlipperBridge flipper;
    ProcedureStore store("/tmp/infiniteai-mobile-invalid");
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    RadioMeasurement measurement;
    MobileApi api(cloud, ai, flipper, store, innovator, measurement);

    auto result = api.accumulateBodyChunkForTest("req2", "{}", 1, 2);
    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, result.error.find("sequence"));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_authentication_requires_bearer_token);
    RUN_TEST(test_fragmented_body_reassembles_in_order);
    RUN_TEST(test_fragmented_body_rejects_invalid_sequence);
    return UNITY_END();
}
