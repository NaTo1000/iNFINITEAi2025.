#include <unity.h>
#include <filesystem>
#include <string>

#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"
#include "innovator/sandbox.h"
#include "innovator/firmware_innovator.h"
#include "procedures/procedure_store.h"

namespace fs = std::filesystem;

void setUp(void) {}
void tearDown(void) {}

void test_innovator_persists_validated_procedure() {
    const std::string root = "/tmp/infiniteai-innovator-pass";
    fs::remove_all(root);
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, "{\"verdict\":\"PROPOSE\",\"summary\":\"Collect telemetry\",\"procedure\":{\"title\":\"Check link\",\"steps\":[{\"action\":\"log\",\"value\":\"capture status\"}],\"validation\":[\"mqtt connected after loop\"]}}", ""});
    cloud.queueMockHttpResponse({true, 200, 1, "{\"verdict\":\"PASS\",\"reason\":\"Validation criteria satisfied\"}", ""});
    AiController ai(cloud);
    ProcedureStore store(root);
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    TEST_ASSERT_TRUE(store.begin());
    innovator.begin();
    innovator.queueTask({"task1", "MQTT reconnect issue", "{}"});
    for (int i = 0; i < 6 && innovator.isBusy(); ++i) {
        innovator.tick();
    }
    TEST_ASSERT_EQUAL_UINT32(1, store.count());
}

void test_innovator_rejects_invalid_procedure() {
    const std::string root = "/tmp/infiniteai-innovator-fail";
    fs::remove_all(root);
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, "{\"verdict\":\"PROPOSE\",\"summary\":\"Unsafe\",\"procedure\":{\"title\":\"Unsafe\",\"steps\":[{\"action\":\"run_cpp\",\"value\":\"hack\"}],\"validation\":[\"none\"]}}", ""});
    cloud.queueMockHttpResponse({true, 200, 1, "{\"verdict\":\"PROPOSE\",\"summary\":\"Unsafe\",\"procedure\":{\"title\":\"Unsafe\",\"steps\":[{\"action\":\"run_cpp\",\"value\":\"hack\"}],\"validation\":[\"none\"]}}", ""});
    AiController ai(cloud);
    ProcedureStore store(root);
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    TEST_ASSERT_TRUE(store.begin());
    innovator.begin();
    innovator.queueTask({"task2", "Unsafe request", "{}"});
    for (int i = 0; i < 12 && innovator.isBusy(); ++i) {
        innovator.tick();
    }
    TEST_ASSERT_EQUAL_UINT32(0, store.count());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_innovator_persists_validated_procedure);
    RUN_TEST(test_innovator_rejects_invalid_procedure);
    return UNITY_END();
}
