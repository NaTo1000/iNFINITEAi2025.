#include <unity.h>
#include <filesystem>
#include <string>

#include "procedures/procedure_store.h"

namespace fs = std::filesystem;

void setUp(void) {}
void tearDown(void) {}

void test_funny_name_is_deterministic() {
    TEST_ASSERT_EQUAL_STRING(ProcedureStore::generateFunnyName(99).c_str(), ProcedureStore::generateFunnyName(99).c_str());
}

void test_save_and_load_round_trip() {
    const std::string root = "/tmp/infiniteai-proc-roundtrip";
    fs::remove_all(root);
    ProcedureStore store(root);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.save(
        "Collect telemetry",
        R"json({"source":{"type":"mqtt","label":"broker-1"},"evidence":[{"id":"ev1","type":"log","source":"device","detail":"captured telemetry"}],"analysis":{"normalizedSummary":"Telemetry was normalized","verifiedFacts":["The reconnect spike is repeatable"],"unsupportedClaims":[],"biasNotes":["Recent incidents dominate the sample"]},"reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],"scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},"qcDecision":"PASS","recommendedRecoveryActions":[{"category":"internal_recovery","detail":"Apply the validated reconnect procedure"}],"recommendationSummary":"Proceed with the evidence-backed reconnect procedure"})json",
        1));
    TEST_ASSERT_EQUAL_UINT32(1, store.count());
    auto procedures = store.loadAll();
    TEST_ASSERT_EQUAL_UINT32(1, procedures.size());
    TEST_ASSERT_EQUAL_STRING("Collect telemetry", procedures[0].description.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, procedures[0].code.find("\"source\""));
}

void test_to_json_exposes_audit_sections() {
    const std::string root = "/tmp/infiniteai-proc-json";
    fs::remove_all(root);
    ProcedureStore store(root);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.save(
        "Collect telemetry",
        R"json({"source":{"type":"mqtt","label":"broker-1"},"evidence":[{"id":"ev1","type":"log","source":"device","detail":"captured telemetry"}],"analysis":{"normalizedSummary":"Telemetry was normalized","verifiedFacts":["The reconnect spike is repeatable"],"unsupportedClaims":[],"biasNotes":["Recent incidents dominate the sample"]},"reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],"scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},"qcDecision":"PASS","recommendedRecoveryActions":[{"category":"internal_recovery","detail":"Apply the validated reconnect procedure"}],"recommendationSummary":"Proceed with the evidence-backed reconnect procedure"})json",
        1));
    const std::string json = store.toJson();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"evidence\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"analysis\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"recommendedRecoveryActions\""));
}

void test_corrupt_index_rebuilds_from_files() {
    const std::string root = "/tmp/infiniteai-proc-rebuild";
    fs::remove_all(root);
    ProcedureStore store(root);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.save("Check mqtt", "{\"steps\":[]}", 1));
    TEST_ASSERT_TRUE(store.corruptIndexForTest("{broken"));
    auto procedures = store.loadAll();
    TEST_ASSERT_EQUAL_UINT32(1, procedures.size());
}

void test_limit_rejects_oversized_procedure() {
    const std::string root = "/tmp/infiniteai-proc-limit";
    fs::remove_all(root);
    ProcedureStore store(root);
    TEST_ASSERT_TRUE(store.begin());
    std::string oversized(PROCEDURE_MAX_CODE_BYTES + 1U, 'x');
    TEST_ASSERT_FALSE(store.save("Too large", oversized, 1));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_funny_name_is_deterministic);
    RUN_TEST(test_save_and_load_round_trip);
    RUN_TEST(test_to_json_exposes_audit_sections);
    RUN_TEST(test_corrupt_index_rebuilds_from_files);
    RUN_TEST(test_limit_rejects_oversized_procedure);
    return UNITY_END();
}
