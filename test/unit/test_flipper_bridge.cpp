// =============================================================================
// test_flipper_bridge.cpp — Unit tests for FlipperBridge packet helpers
// =============================================================================
#define NATIVE_TEST
#include <unity.h>
#include <string>

void setUp(void) {}
void tearDown(void) {}

#include "../src/flipper/flipper_bridge.h"
#include "../src/flipper/flipper_bridge.cpp"

// ---------------------------------------------------------------------------
void test_ping_sends_without_crash() {
    FlipperBridge fb;
    // In native mode, send() just prints; verify it returns true
    TEST_ASSERT_TRUE(fb.ping());
}

void test_nfc_emulate_sends() {
    FlipperBridge fb;
    TEST_ASSERT_TRUE(fb.nfcEmulate("DEADBEEF"));
}

void test_gpio_set_true() {
    FlipperBridge fb;
    TEST_ASSERT_TRUE(fb.gpioSet(5, true));
}

void test_gpio_set_false() {
    FlipperBridge fb;
    TEST_ASSERT_TRUE(fb.gpioSet(5, false));
}

void test_custom_send() {
    FlipperBridge fb;
    TEST_ASSERT_TRUE(fb.sendCustom("{\"test\":1}"));
}

void test_not_connected_initially() {
    FlipperBridge fb;
    // No ACK received → not connected
    TEST_ASSERT_FALSE(fb.isConnected());
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ping_sends_without_crash);
    RUN_TEST(test_nfc_emulate_sends);
    RUN_TEST(test_gpio_set_true);
    RUN_TEST(test_gpio_set_false);
    RUN_TEST(test_custom_send);
    RUN_TEST(test_not_connected_initially);
    return UNITY_END();
}
