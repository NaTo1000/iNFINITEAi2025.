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
    cloud.queueMockHttpResponse({true, 200, 1, R"json(
{"verdict":"PROPOSE","summary":"Collect telemetry",
 "procedure":{"title":"Check link","steps":[{"action":"log","value":"capture status"}],"validation":["mqtt connected after loop"]},
 "report":{"source":{"type":"mqtt","label":"broker-1"},
 "evidence":[{"id":"ev1","type":"log","source":"device","detail":"mqtt reconnect telemetry"}],
 "analysis":{"normalizedSummary":"Reconnect telemetry was normalized","verifiedFacts":["MQTT reconnect is slower than baseline"],"unsupportedClaims":[],"biasNotes":["Provider may overweight the newest incident"]},
 "reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],
 "scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},
 "qcDecision":"PASS",
 "recovery":[{"category":"internal_recovery","detail":"Apply the reconnect recovery procedure"}],
 "recommendationSummary":"Proceed with the evidence-backed reconnect procedure"}})json", ""});
    cloud.queueMockHttpResponse({true, 200, 1, R"json(
{"verdict":"PASS","reason":"Validation criteria satisfied",
 "report":{"source":{"type":"mqtt","label":"broker-1"},
 "evidence":[{"id":"ev1","type":"log","source":"device","detail":"mqtt reconnect telemetry"}],
 "analysis":{"normalizedSummary":"Reconnect telemetry was normalized","verifiedFacts":["MQTT reconnect is slower than baseline"],"unsupportedClaims":[],"biasNotes":["Provider may overweight the newest incident"]},
 "reviewers":[{"model":"model-a","verdict":"PASS","confidence":96,"notes":"Evidence is concrete"},{"model":"model-b","verdict":"PASS","confidence":94,"notes":"Cross-check matches the logs"}],
 "scores":{"quality":95,"confidence":94,"contradiction":10,"deception":8},
 "qcDecision":"PASS",
 "recovery":[{"category":"internal_recovery","detail":"Apply the reconnect recovery procedure"}],
 "recommendationSummary":"Proceed with the evidence-backed reconnect procedure"}})json", ""});
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
    auto procedures = store.loadAll();
    TEST_ASSERT_EQUAL_UINT32(1, procedures.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, procedures[0].code.find("\"evidence\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, procedures[0].code.find("\"recommendedRecoveryActions\""));
}

void test_innovator_scraps_low_quality_review() {
    const std::string root = "/tmp/infiniteai-innovator-scrap";
    fs::remove_all(root);
    CloudManager cloud;
    cloud.queueMockHttpResponse({true, 200, 1, R"json(
{"verdict":"PROPOSE","summary":"Collect telemetry",
 "procedure":{"title":"Check link","steps":[{"action":"log","value":"capture status"}],"validation":["mqtt connected after loop"]},
 "report":{"source":{"type":"mqtt","label":"broker-2"},
 "evidence":[{"id":"ev2","type":"claim","source":"external","detail":"Unverified complaint text"}],
 "analysis":{"normalizedSummary":"The ingestion mixes claims and sparse logs","verifiedFacts":["Only one weak claim was provided"],"unsupportedClaims":[],"biasNotes":["Sparse samples amplify anecdotal bias"]},
 "reviewers":[{"model":"model-a","verdict":"FAIL","confidence":60,"notes":"Evidence quality is weak"},{"model":"model-b","verdict":"FAIL","confidence":55,"notes":"Confidence is below threshold"}],
 "scores":{"quality":50,"confidence":48,"contradiction":12,"deception":14},
 "qcDecision":"REBUILD",
 "recovery":[{"category":"no_action","detail":"Collect more concrete evidence before acting"}],
 "recommendationSummary":"Rebuild the ingestion with stronger evidence"}})json", ""});
    cloud.queueMockHttpResponse({true, 200, 1, R"json(
{"verdict":"FAIL","reason":"Evidence quality remains below threshold",
 "report":{"source":{"type":"mqtt","label":"broker-2"},
 "evidence":[{"id":"ev2","type":"claim","source":"external","detail":"Unverified complaint text"}],
 "analysis":{"normalizedSummary":"The ingestion mixes claims and sparse logs","verifiedFacts":["Only one weak claim was provided"],"unsupportedClaims":["The complaint alleged fraud without evidence"],"biasNotes":["Sparse samples amplify anecdotal bias"]},
 "reviewers":[{"model":"model-a","verdict":"FAIL","confidence":60,"notes":"Evidence quality is weak"},{"model":"model-b","verdict":"FAIL","confidence":55,"notes":"Confidence is below threshold"}],
 "scores":{"quality":45,"confidence":44,"contradiction":18,"deception":19},
 "qcDecision":"SCRAP",
 "recovery":[{"category":"no_action","detail":"Discard and recollect the ingestion"}],
 "recommendationSummary":"Scrap the low-quality ingestion"}})json", ""});
    AiController ai(cloud);
    ProcedureStore store(root);
    Sandbox sandbox;
    FirmwareInnovator innovator(ai, cloud, store, sandbox);
    TEST_ASSERT_TRUE(store.begin());
    innovator.begin();
    innovator.queueTask({"task2", "Unsafe request", "{}"});
    for (int i = 0; i < 8 && innovator.isBusy(); ++i) {
        innovator.tick();
    }
    TEST_ASSERT_EQUAL_UINT32(0, store.count());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, innovator.lastReportJson().find("\"qcDecision\":\"SCRAP\""));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_innovator_persists_validated_procedure);
    RUN_TEST(test_innovator_scraps_low_quality_review);
    return UNITY_END();
}
