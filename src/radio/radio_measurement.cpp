#include "radio_measurement.h"

#include "../common/native_json.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

constexpr float kMinSides = 3.0f;
constexpr float kMaxSides = 100000.0f;
constexpr float kMinToleranceDeg = 0.0005f;
constexpr float kMaxToleranceDeg = 45.0f;
constexpr float kMaxDistanceMeters = 10000.0f;
constexpr float kMinFrequencyHz = static_cast<float>(FLIPPER_MIN_RF_HZ);
constexpr float kMaxFrequencyHz = static_cast<float>(FLIPPER_MAX_RF_HZ);
constexpr double kPi = 3.14159265358979323846;

std::string formatFloat(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

} // namespace

bool RadioMeasurement::start(const RadioMeasurementConfig& config, std::string& error) {
    RadioMeasurementConfig normalized;
    if (!_normalizeAndValidateConfig(config, normalized, error)) {
        _lastError = error;
        _active = false;
        _ready = false;
        _samples.clear();
        _latestSampleTimestampMs = 0;
        _summary = {};
        _summary.status = "invalid_config";
        return false;
    }

    _config = normalized;
    _samples.clear();
    _latestSampleTimestampMs = 0;
    _active = true;
    _ready = false;
    _lastError.clear();
    _summary = {};
    _summary.status = "collecting";
    return true;
}

bool RadioMeasurement::ingestSample(const RadioSignalSample& sample, std::string& error) {
    if (!_active) {
        error = "measurement_inactive";
        _lastError = error;
        return false;
    }
    if (!_validateSample(sample, error)) {
        _lastError = error;
        return false;
    }
    if (_latestSampleTimestampMs != 0 &&
        sample.timestampMs + _config.maxSampleAgeMs < _latestSampleTimestampMs) {
        error = "stale_sample_rejected";
        _lastError = error;
        return false;
    }

    _samples.push_back(sample);
    _latestSampleTimestampMs = std::max(_latestSampleTimestampMs, sample.timestampMs);
    _pruneSamples();
    _recomputeSummary();
    _lastError.clear();
    error.clear();
    return true;
}

void RadioMeasurement::reset() {
    _samples.clear();
    _latestSampleTimestampMs = 0;
    _active = false;
    _ready = false;
    _lastError.clear();
    _summary = {};
    _summary.status = "inactive";
}

std::string RadioMeasurement::toJson() const {
    std::string out = "{";
    out += "\"active\":" + std::string(_active ? "true" : "false");
    out += ",\"ready\":" + std::string(_ready ? "true" : "false");
    out += ",\"receiver\":{";
    out += "\"frequency_hz\":" + std::to_string(_config.frequencyHz);
    out += ",\"sample_window_ms\":" + std::to_string(_config.sampleWindowMs);
    out += ",\"max_sample_age_ms\":" + std::to_string(_config.maxSampleAgeMs);
    out += ",\"required_samples\":" + std::to_string(_config.requiredSamples);
    out += ",\"samples_collected\":" + std::to_string(_samples.size());
    out += ",\"last_sample_ms\":" + std::to_string(_latestSampleTimestampMs);
    out += ",\"transmitter_active\":" + std::string(_config.transmitterActive ? "true" : "false");
    out += "},\"measurement\":{";
    out += "\"mode\":\"" + std::string(modeToString(_config.mode)) + "\"";
    out += ",\"status\":\"" + nativejson::escapeString(_summary.status) + "\"";
    out += ",\"target_sides\":" + formatFloat(_config.targetSides);
    out += ",\"target_exterior_angle_deg\":" + formatFloat(_summary.targetExteriorAngleDeg);
    out += ",\"target_interior_angle_deg\":" + formatFloat(_summary.targetInteriorAngleDeg);
    out += ",\"angle_tolerance_deg\":" + formatFloat(_config.angleToleranceDeg);
    out += ",\"calibration_offset_deg\":" + formatFloat(_config.calibrationOffsetDeg);
    out += ",\"mean_bearing_deg\":" + formatFloat(_summary.meanBearingDeg);
    out += ",\"corrected_bearing_deg\":" + formatFloat(_summary.correctedBearingDeg);
    out += ",\"spread_deg\":" + formatFloat(_summary.spreadDeg);
    out += ",\"angle_error_deg\":" + formatFloat(_summary.angleErrorDeg);
    out += ",\"distance_mean_m\":" + formatFloat(_summary.meanDistanceMeters);
    out += ",\"estimated_polygon_sides\":" + formatFloat(_summary.estimatedPolygonSides);
    out += ",\"repeatable\":" + std::string(_summary.repeatable ? "true" : "false");
    out += ",\"fresh\":" + std::string(_summary.fresh ? "true" : "false");
    out += "},\"error\":\"" + nativejson::escapeString(_lastError) + "\"}";
    return out;
}

const char* RadioMeasurement::modeToString(RadioMeasurementMode mode) {
    switch (mode) {
        case RadioMeasurementMode::ANGLE_OF_ARRIVAL:
            return "angle_of_arrival";
        case RadioMeasurementMode::ORIENTATION:
            return "orientation";
        case RadioMeasurementMode::POLYGON_SCALE:
            return "polygon_scale";
    }
    return "polygon_scale";
}

bool RadioMeasurement::parseMode(const std::string& value, RadioMeasurementMode& out) {
    if (value == "angle_of_arrival") {
        out = RadioMeasurementMode::ANGLE_OF_ARRIVAL;
        return true;
    }
    if (value == "orientation") {
        out = RadioMeasurementMode::ORIENTATION;
        return true;
    }
    if (value == "polygon_scale") {
        out = RadioMeasurementMode::POLYGON_SCALE;
        return true;
    }
    return false;
}

bool RadioMeasurement::_normalizeAndValidateConfig(const RadioMeasurementConfig& config,
                                                   RadioMeasurementConfig& normalized,
                                                   std::string& error) const {
    normalized = config;
    if (normalized.frequencyHz == 0) {
        normalized.frequencyHz = RADIO_MEASURE_DEFAULT_FREQ_HZ;
    }
    if (normalized.sampleWindowMs == 0) {
        normalized.sampleWindowMs = RADIO_MEASURE_DEFAULT_WINDOW_MS;
    }
    if (normalized.maxSampleAgeMs == 0) {
        normalized.maxSampleAgeMs = std::min<uint32_t>(
            RADIO_MEASURE_DEFAULT_MAX_SAMPLE_AGE_MS,
            normalized.sampleWindowMs);
    }
    if (normalized.requiredSamples == 0) {
        normalized.requiredSamples = RADIO_MEASURE_DEFAULT_REQUIRED_SAMPLES;
    }
    if (normalized.targetSides == 0.0f) {
        normalized.targetSides = 10000.0f;
    }
    if (normalized.frequencyHz < static_cast<uint32_t>(kMinFrequencyHz) ||
        normalized.frequencyHz > static_cast<uint32_t>(kMaxFrequencyHz)) {
        error = "frequency_out_of_range";
        return false;
    }
    if (normalized.sampleWindowMs < 50U || normalized.sampleWindowMs > 60000U) {
        error = "sample_window_out_of_range";
        return false;
    }
    if (normalized.maxSampleAgeMs == 0U ||
        normalized.maxSampleAgeMs > normalized.sampleWindowMs) {
        error = "max_sample_age_out_of_range";
        return false;
    }
    if (normalized.requiredSamples < 2U ||
        normalized.requiredSamples > RADIO_MEASURE_MAX_SAMPLES) {
        error = "required_samples_out_of_range";
        return false;
    }
    if (std::fabs(normalized.calibrationOffsetDeg) > 180.0f) {
        error = "calibration_offset_out_of_range";
        return false;
    }
    if (normalized.repeatabilityToleranceDeg < kMinToleranceDeg ||
        normalized.repeatabilityToleranceDeg > kMaxToleranceDeg) {
        error = "repeatability_tolerance_out_of_range";
        return false;
    }
    if (normalized.targetSides < kMinSides || normalized.targetSides > kMaxSides) {
        error = "target_sides_out_of_range";
        return false;
    }

    const float targetExterior = 360.0f / normalized.targetSides;
    if (normalized.angleToleranceDeg <= 0.0f) {
        normalized.angleToleranceDeg = std::max(kMinToleranceDeg, targetExterior * 0.5f);
    }
    if (normalized.angleToleranceDeg < kMinToleranceDeg ||
        normalized.angleToleranceDeg > kMaxToleranceDeg) {
        error = "angle_tolerance_out_of_range";
        return false;
    }
    return true;
}

bool RadioMeasurement::_validateSample(const RadioSignalSample& sample, std::string& error) const {
    if (sample.timestampMs == 0U) {
        error = "timestamp_required";
        return false;
    }
    if (sample.bearingDeg < 0.0f || sample.bearingDeg >= 360.0f) {
        error = "bearing_out_of_range";
        return false;
    }
    if (sample.rssiDbm < -140 || sample.rssiDbm > 10) {
        error = "rssi_out_of_range";
        return false;
    }
    if (sample.distanceMeters < 0.0f || sample.distanceMeters > kMaxDistanceMeters) {
        error = "distance_out_of_range";
        return false;
    }
    if (sample.quality == 0U) {
        error = "quality_required";
        return false;
    }
    return true;
}

void RadioMeasurement::_pruneSamples() {
    if (_latestSampleTimestampMs == 0) {
        return;
    }
    const uint32_t cutoff = (_latestSampleTimestampMs > _config.sampleWindowMs)
        ? (_latestSampleTimestampMs - _config.sampleWindowMs)
        : 0U;
    _samples.erase(
        std::remove_if(_samples.begin(), _samples.end(),
            [&](const RadioSignalSample& sample) {
                return sample.timestampMs < cutoff;
            }),
        _samples.end());
    if (_samples.size() > RADIO_MEASURE_MAX_SAMPLES) {
        _samples.erase(_samples.begin(), _samples.end() - RADIO_MEASURE_MAX_SAMPLES);
    }
}

void RadioMeasurement::_recomputeSummary() {
    Summary updated;
    updated.status = _active ? "collecting" : "inactive";
    updated.targetExteriorAngleDeg = 360.0f / _config.targetSides;
    updated.targetInteriorAngleDeg = 180.0f * (_config.targetSides - 2.0f) / _config.targetSides;

    if (_samples.empty()) {
        _summary = updated;
        _ready = false;
        return;
    }

    double sumSin = 0.0;
    double sumCos = 0.0;
    double distanceSum = 0.0;
    for (const RadioSignalSample& sample : _samples) {
        const double radians = static_cast<double>(sample.bearingDeg) * kPi / 180.0;
        sumSin += std::sin(radians);
        sumCos += std::cos(radians);
        distanceSum += sample.distanceMeters;
    }

    float meanBearing = static_cast<float>(std::atan2(sumSin, sumCos) * 180.0 / kPi);
    meanBearing = _normalizeAngle(meanBearing);
    float spread = 0.0f;
    for (const RadioSignalSample& sample : _samples) {
        spread = std::max(spread, std::fabs(_angularDelta(sample.bearingDeg, meanBearing)));
    }

    updated.meanBearingDeg = meanBearing;
    updated.correctedBearingDeg = _normalizeAngle(meanBearing + _config.calibrationOffsetDeg);
    updated.spreadDeg = spread;
    updated.meanDistanceMeters = static_cast<float>(distanceSum / static_cast<double>(_samples.size()));
    updated.estimatedPolygonSides =
        updated.correctedBearingDeg > 0.0001f ? 360.0f / updated.correctedBearingDeg : 0.0f;
    updated.angleErrorDeg = std::fabs(
        _angularDelta(updated.correctedBearingDeg, updated.targetExteriorAngleDeg));
    updated.fresh = true;
    updated.repeatable =
        _samples.size() >= _config.requiredSamples &&
        updated.spreadDeg <= _config.repeatabilityToleranceDeg;

    const bool angleWithinTolerance =
        _config.mode != RadioMeasurementMode::POLYGON_SCALE ||
        updated.angleErrorDeg <= _config.angleToleranceDeg;

    _ready = updated.repeatable && angleWithinTolerance;
    updated.status = _ready ? "ready" : (updated.repeatable ? "out_of_tolerance" : "collecting");
    _summary = updated;
}

float RadioMeasurement::_normalizeAngle(float degrees) {
    while (degrees < 0.0f) {
        degrees += 360.0f;
    }
    while (degrees >= 360.0f) {
        degrees -= 360.0f;
    }
    return degrees;
}

float RadioMeasurement::_angularDelta(float aDeg, float bDeg) {
    float delta = _normalizeAngle(aDeg) - _normalizeAngle(bDeg);
    while (delta > 180.0f) {
        delta -= 360.0f;
    }
    while (delta < -180.0f) {
        delta += 360.0f;
    }
    return delta;
}
