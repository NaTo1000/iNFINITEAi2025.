#pragma once
// =============================================================================
// mobile_api.h — HTTP REST + BLE interface for mobile app and PC
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../config.h"

class CloudManager;
class AiController;
class FlipperBridge;
class ProcedureStore;
class FirmwareInnovator;
class RadioMeasurement;

using ApiCommandCallback = std::function<void(const std::string& jsonCommand)>;

class MobileApi {
public:
    MobileApi(CloudManager& cloud,
              AiController& ai,
              FlipperBridge& flipper,
              ProcedureStore& store,
              FirmwareInnovator& innovator,
              RadioMeasurement& measurement);

    bool begin();
    void loop();
    void onCommand(ApiCommandCallback cb) { _cmdCb = cb; }
    void notifyBle(const std::string& json);
    void _onBleWrite(const std::string& data);

#ifdef NATIVE_TEST
    bool authenticateForTest(const std::string& authHeader) const { return _authenticate(authHeader); }
    struct BodyChunkResult {
        bool accepted = false;
        bool complete = false;
        std::string body;
        std::string error;
    };
    BodyChunkResult accumulateBodyChunkForTest(const std::string& requestId,
                                               const std::string& chunk,
                                               size_t index,
                                               size_t total);
#endif

private:
    struct BufferedRequest {
        std::string data;
        size_t      total = 0;
    };

    void _setupRoutes();
    bool _authenticate(const std::string& authHeader) const;
    bool _rateLimitOk();
    void _setupBle();
    std::string _requestKey(const void* request) const;
    bool _appendBodyChunk(const std::string& requestKey,
                          const uint8_t* data,
                          size_t len,
                          size_t index,
                          size_t total,
                          std::string& completeBody,
                          std::string& error);
    void _clearRequestBuffer(const std::string& requestKey);
    uint32_t _nowMs() const;

    CloudManager&  _cloud;
    AiController&  _ai;
    FlipperBridge& _flipper;
    ProcedureStore& _store;
    FirmwareInnovator& _innovator;
    RadioMeasurement& _measurement;
    ApiCommandCallback _cmdCb;
    std::map<std::string, BufferedRequest> _requestBuffers;
    std::vector<uint32_t> _recentCallTimes;

#ifndef NATIVE_TEST
    void* _server = nullptr;
    void* _bleChar = nullptr;
#endif
};
