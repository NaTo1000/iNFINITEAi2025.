// =============================================================================
// test_sandbox.cpp — Unit tests for Sandbox (native)
// =============================================================================
#define NATIVE_TEST
#include <unity.h>
#include <stdexcept>
#include <string>

void setUp(void) {}
void tearDown(void) {}

#include "../src/innovator/sandbox.h"
#include "../src/innovator/sandbox.cpp"

// ---------------------------------------------------------------------------
void test_sandbox_passes_on_success() {
    Sandbox sb;
    SandboxResult r = sb.run("{}", [](const std::string&) -> std::string {
        return "OK";
    });
    TEST_ASSERT_TRUE(r.passed);
    TEST_ASSERT_EQUAL_STRING("OK", r.output.c_str());
    TEST_ASSERT_TRUE(r.error.empty());
}

void test_sandbox_catches_exception() {
    Sandbox sb;
    SandboxResult r = sb.run("{}", [](const std::string&) -> std::string {
        throw std::runtime_error("intentional error");
        return "";
    });
    TEST_ASSERT_FALSE(r.passed);
    TEST_ASSERT_FALSE(r.error.empty());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.error.find("intentional error"));
}

void test_sandbox_has_headroom() {
    Sandbox sb;
    // In native build _getHeap() returns 80KB, limit is 40KB
    TEST_ASSERT_TRUE(sb.hasHeadroom());
}

void test_sandbox_free_heap_positive() {
    Sandbox sb;
    TEST_ASSERT_GREATER_THAN(0u, sb.freeHeap());
}

void test_sandbox_context_passed_to_test() {
    Sandbox sb;
    std::string ctx = "{\"key\":\"value\"}";
    SandboxResult r = sb.run(ctx, [](const std::string& c) -> std::string {
        return c;  // echo context
    });
    TEST_ASSERT_TRUE(r.passed);
    TEST_ASSERT_EQUAL_STRING(ctx.c_str(), r.output.c_str());
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_sandbox_passes_on_success);
    RUN_TEST(test_sandbox_catches_exception);
    RUN_TEST(test_sandbox_has_headroom);
    RUN_TEST(test_sandbox_free_heap_positive);
    RUN_TEST(test_sandbox_context_passed_to_test);
    return UNITY_END();
}
