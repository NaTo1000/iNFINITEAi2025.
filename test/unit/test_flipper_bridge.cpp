#include <unity.h>
#include <string>

#include "flipper/flipper_bridge.h"

void setUp(void) {}
void tearDown(void) {}

void test_begin_requires_ack() {
    FlipperBridge fb;
    TEST_ASSERT_FALSE(fb.begin());
    TEST_ASSERT_FALSE(fb.isConnected());
}

void test_process_ack_marks_connected() {
    FlipperBridge fb;
    TEST_ASSERT_TRUE(fb.ping());
    TEST_ASSERT_TRUE(fb.processIncomingForTest("{\"cmd\":240,\"payload\":{}}"));
    TEST_ASSERT_TRUE(fb.isConnected());
}

void test_rejects_invalid_rf_payload() {
    FlipperBridge fb;
    TEST_ASSERT_FALSE(fb.rfTransmit("GG", 100));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, fb.lastError().find("Invalid RF"));
}

void test_rejects_oversized_custom_frame() {
    FlipperBridge fb;
    std::string payload = "{\"data\":\"" + std::string(600, 'a') + "\"}";
    TEST_ASSERT_FALSE(fb.sendCustom(payload));
}

void test_malformed_incoming_frame_sets_error() {
    FlipperBridge fb;
    TEST_ASSERT_FALSE(fb.processIncomingForTest("not-json"));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_requires_ack);
    RUN_TEST(test_process_ack_marks_connected);
    RUN_TEST(test_rejects_invalid_rf_payload);
    RUN_TEST(test_rejects_oversized_custom_frame);
    RUN_TEST(test_malformed_incoming_frame_sets_error);
    return UNITY_END();
}
