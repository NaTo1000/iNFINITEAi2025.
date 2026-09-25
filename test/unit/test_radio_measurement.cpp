#include <unity.h>

#include <string>

#include "radio/radio_measurement.h"

void setUp(void) {}
void tearDown(void) {}

void test_start_rejects_invalid_polygon_target() {
    RadioMeasurement measurement;
    RadioMeasurementConfig config;
    config.targetSides = 2.0f;
    std::string error;
    TEST_ASSERT_FALSE(measurement.start(config, error));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("target_sides"));
}

void test_start_normalizes_zero_values() {
    RadioMeasurement measurement;
    RadioMeasurementConfig config;
    config.frequencyHz = 0;
    config.sampleWindowMs = 0;
    config.maxSampleAgeMs = 0;
    config.requiredSamples = 0;
    config.angleToleranceDeg = 0.0f;
    std::string error;
    TEST_ASSERT_TRUE(measurement.start(config, error));
    TEST_ASSERT_EQUAL_UINT32(RADIO_MEASURE_DEFAULT_FREQ_HZ, measurement.configForTest().frequencyHz);
    TEST_ASSERT_EQUAL_UINT32(RADIO_MEASURE_DEFAULT_WINDOW_MS, measurement.configForTest().sampleWindowMs);
    TEST_ASSERT_EQUAL_UINT32(RADIO_MEASURE_DEFAULT_REQUIRED_SAMPLES, measurement.configForTest().requiredSamples);
    TEST_ASSERT_TRUE(measurement.configForTest().angleToleranceDeg > 0.0f);
}

void test_stale_samples_are_rejected() {
    RadioMeasurement measurement;
    RadioMeasurementConfig config;
    config.maxSampleAgeMs = 100;
    std::string error;
    TEST_ASSERT_TRUE(measurement.start(config, error));
    TEST_ASSERT_TRUE(measurement.ingestSample({0.0360f, -70, 1.2f, 1000U, 90U}, error));
    TEST_ASSERT_FALSE(measurement.ingestSample({0.0362f, -71, 1.1f, 850U, 88U}, error));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("stale"));
}

void test_repeatable_polygon_measurement_becomes_ready() {
    RadioMeasurement measurement;
    RadioMeasurementConfig config;
    config.requiredSamples = 3;
    config.repeatabilityToleranceDeg = 0.01f;
    config.angleToleranceDeg = 0.02f;
    std::string error;
    TEST_ASSERT_TRUE(measurement.start(config, error));
    TEST_ASSERT_TRUE(measurement.ingestSample({0.0360f, -70, 1.2f, 1000U, 95U}, error));
    TEST_ASSERT_TRUE(measurement.ingestSample({0.0361f, -69, 1.1f, 1100U, 94U}, error));
    TEST_ASSERT_TRUE(measurement.ingestSample({0.0359f, -71, 1.3f, 1200U, 96U}, error));
    TEST_ASSERT_TRUE(measurement.isReady());
    const std::string json = measurement.toJson();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"status\":\"ready\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"target_exterior_angle_deg\":0.036000"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"samples_collected\":3"));
}

void test_reset_clears_state() {
    RadioMeasurement measurement;
    RadioMeasurementConfig config;
    std::string error;
    TEST_ASSERT_TRUE(measurement.start(config, error));
    measurement.reset();
    TEST_ASSERT_FALSE(measurement.isActive());
    TEST_ASSERT_FALSE(measurement.isReady());
    TEST_ASSERT_EQUAL_UINT32(0, measurement.sampleCount());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_start_rejects_invalid_polygon_target);
    RUN_TEST(test_start_normalizes_zero_values);
    RUN_TEST(test_stale_samples_are_rejected);
    RUN_TEST(test_repeatable_polygon_measurement_becomes_ready);
    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
