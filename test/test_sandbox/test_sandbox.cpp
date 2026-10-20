#include <unity.h>
#include <stdexcept>
#include <string>

#include "innovator/sandbox.h"

void setUp(void) {}
void tearDown(void) {}

void test_sandbox_passes_on_success() {
    Sandbox sb;
    SandboxResult r = sb.run("{}", [](const std::string&) -> std::string {
        return "OK";
    });
    TEST_ASSERT_TRUE(r.passed);
    TEST_ASSERT_EQUAL_STRING("OK", r.output.c_str());
}

void test_sandbox_catches_exception() {
    Sandbox sb;
    SandboxResult r = sb.run("{}", [](const std::string&) -> std::string {
        throw std::runtime_error("intentional error");
    });
    TEST_ASSERT_FALSE(r.passed);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.error.find("intentional error"));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_sandbox_passes_on_success);
    RUN_TEST(test_sandbox_catches_exception);
    return UNITY_END();
}
