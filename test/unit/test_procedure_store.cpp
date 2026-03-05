// =============================================================================
// test_procedure_store.cpp — Unit tests for ProcedureStore (native)
// =============================================================================
#define NATIVE_TEST
#include <unity.h>
#include <string>
#include <cstdint>

void setUp(void) {}
void tearDown(void) {}

// Pull in the implementation under test
#include "../src/procedures/procedure_store.h"
#include "../src/procedures/procedure_store.cpp"

// ---------------------------------------------------------------------------
void test_funny_name_is_not_empty() {
    std::string name = ProcedureStore::generateFunnyName(42);
    TEST_ASSERT_FALSE(name.empty());
}

void test_funny_name_contains_version() {
    std::string name = ProcedureStore::generateFunnyName(42);
    // Must contain 'V' followed by digits
    size_t vPos = name.find('V');
    TEST_ASSERT_NOT_EQUAL(std::string::npos, vPos);
    TEST_ASSERT_TRUE(vPos + 1 < name.size());
    TEST_ASSERT_TRUE(std::isdigit(static_cast<unsigned char>(name[vPos + 1])));
}

void test_different_seeds_give_different_names() {
    std::string a = ProcedureStore::generateFunnyName(1);
    std::string b = ProcedureStore::generateFunnyName(1000000);
    TEST_ASSERT_FALSE(a == b);
}

void test_same_seed_deterministic() {
    std::string a = ProcedureStore::generateFunnyName(99);
    std::string b = ProcedureStore::generateFunnyName(99);
    TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

void test_store_initial_count_zero() {
    ProcedureStore store;
    TEST_ASSERT_EQUAL(0, store.count());
}

void test_to_json_empty_is_valid() {
    ProcedureStore store;
    std::string json = store.toJson();
    TEST_ASSERT_FALSE(json.empty());
    // Should contain "procedures"
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("procedures"));
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_funny_name_is_not_empty);
    RUN_TEST(test_funny_name_contains_version);
    RUN_TEST(test_different_seeds_give_different_names);
    RUN_TEST(test_same_seed_deterministic);
    RUN_TEST(test_store_initial_count_zero);
    RUN_TEST(test_to_json_empty_is_valid);
    return UNITY_END();
}
