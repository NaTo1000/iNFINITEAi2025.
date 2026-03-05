#pragma once
// =============================================================================
// mobile_api.h — HTTP REST + BLE interface for mobile app and PC
//
// Exposes:
//   GET  /status        — device health and last AI decision
//   POST /command       — send a command (JSON body)
//   GET  /procedures    — list saved innovation procedures
//   POST /innovate      — trigger an innovator cycle
//   GET  /innovate/log  — retrieve innovator log
//   POST /ai/query      — direct AI query
//
// All endpoints require "Authorization: Bearer <API_AUTH_TOKEN>" header.
// BLE GATT characteristic mirrors the /command endpoint.
// =============================================================================
#include <string>
#include <functional>
#include "../config.h"

// Forward declarations
class CloudManager;
class AiController;
class FlipperBridge;

using ApiCommandCallback = std::function<void(const std::string& jsonCommand)>;

class MobileApi {
public:
    MobileApi(CloudManager& cloud, AiController& ai, FlipperBridge& flipper);

    bool begin();
    void loop();

    // Register handler for commands arriving via REST or BLE
    void onCommand(ApiCommandCallback cb) { _cmdCb = cb; }

    // Send a notification via BLE (e.g. AI result or status update)
    void notifyBle(const std::string& json);

private:
    void _setupRoutes();
    bool _authenticate(const std::string& authHeader) const;
    void _setupBle();
    void _onBleWrite(const std::string& data);

    CloudManager&  _cloud;
    AiController&  _ai;
    FlipperBridge& _flipper;
    ApiCommandCallback _cmdCb;

#ifndef NATIVE_TEST
    // Defined in .cpp to avoid including AsyncWebServer in header (large dep)
    void* _server = nullptr;
    void* _bleChar = nullptr;
#endif
};
