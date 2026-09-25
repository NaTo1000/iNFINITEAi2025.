#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../config.h"

enum class RadioMeasurementMode : uint8_t {
    ANGLE_OF_ARRIVAL = 0,
    ORIENTATION = 1,
    POLYGON_SCALE = 2,
};

struct RadioMeasurementConfig {
    RadioMeasurementMode mode = RadioMeasurementMode::POLYGON_SCALE;
    uint32_t frequencyHz = RADIO_MEASURE_DEFAULT_FREQ_HZ;
    uint32_t sampleWindowMs = RADIO_MEASURE_DEFAULT_WINDOW_MS;
    uint32_t maxSampleAgeMs = RADIO_MEASURE_DEFAULT_MAX_SAMPLE_AGE_MS;
    uint16_t requiredSamples = RADIO_MEASURE_DEFAULT_REQUIRED_SAMPLES;
    float calibrationOffsetDeg = 0.0f;
    float repeatabilityToleranceDeg = 0.5f;
    float angleToleranceDeg = 0.0f;
    float targetSides = 10000.0f;
    bool transmitterActive = false;
};

struct RadioSignalSample {
    float bearingDeg = 0.0f;
    int16_t rssiDbm = -120;
    float distanceMeters = 0.0f;
    uint32_t timestampMs = 0;
    uint8_t quality = 100;
};

class RadioMeasurement {
public:
    bool start(const RadioMeasurementConfig& config, std::string& error);
    bool ingestSample(const RadioSignalSample& sample, std::string& error);
    void reset();

    bool isActive() const { return _active; }
    bool isReady() const { return _ready; }
    size_t sampleCount() const { return _samples.size(); }
    const std::string& lastError() const { return _lastError; }
    std::string toJson() const;

    static const char* modeToString(RadioMeasurementMode mode);
    static bool parseMode(const std::string& value, RadioMeasurementMode& out);

#ifdef NATIVE_TEST
    const RadioMeasurementConfig& configForTest() const { return _config; }
#endif

private:
    struct Summary {
        float meanBearingDeg = 0.0f;
        float correctedBearingDeg = 0.0f;
        float spreadDeg = 0.0f;
        float meanDistanceMeters = 0.0f;
        float estimatedPolygonSides = 0.0f;
        float targetExteriorAngleDeg = 0.0f;
        float targetInteriorAngleDeg = 0.0f;
        float angleErrorDeg = 0.0f;
        bool repeatable = false;
        bool fresh = false;
        std::string status = "inactive";
    };

    RadioMeasurementConfig _config;
    std::vector<RadioSignalSample> _samples;
    uint32_t _latestSampleTimestampMs = 0;
    bool _active = false;
    bool _ready = false;
    std::string _lastError;
    Summary _summary;

    bool _normalizeAndValidateConfig(const RadioMeasurementConfig& config,
                                     RadioMeasurementConfig& normalized,
                                     std::string& error) const;
    bool _validateSample(const RadioSignalSample& sample, std::string& error) const;
    void _pruneSamples();
    void _recomputeSummary();

    static float _normalizeAngle(float degrees);
    static float _angularDelta(float aDeg, float bDeg);
};
