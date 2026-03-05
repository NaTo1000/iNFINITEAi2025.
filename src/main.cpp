// =============================================================================
// main.cpp — iNFINITEAi2025 ESP32 firmware entry point
//
// Architecture:
//   ┌─────────────────────────────────────────────────────────┐
//   │  CloudManager  ←→  MQTT broker / REST cloud services   │
//   │  AiController  ←→  LLM API (OpenAI-compatible)         │
//   │  FlipperBridge ←→  Flipper Zero (UART + BLE)           │
//   │  MobileApi     ←→  Mobile app / PC (HTTP + BLE GATT)   │
//   │  FirmwareInnovator ← AI-driven self-improvement loop    │
//   │    └─ Sandbox       (safe test execution)               │
//   │    └─ ProcedureStore (funny-named innovation archive)   │
//   └─────────────────────────────────────────────────────────┘
// =============================================================================
#ifndef NATIVE_TEST

#include <Arduino.h>
#include <esp_log.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#include "config.h"
#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"
#include "flipper/flipper_bridge.h"
#include "mobile/mobile_api.h"
#include "innovator/sandbox.h"
#include "innovator/firmware_innovator.h"
#include "procedures/procedure_store.h"

// ---------------------------------------------------------------------------
// Module instances
// ---------------------------------------------------------------------------
static CloudManager    cloudMgr;
static AiController    aiCtrl(cloudMgr);
static FlipperBridge   flipper;
static MobileApi       mobileApi(cloudMgr, aiCtrl, flipper);
static ProcedureStore  procStore;
static Sandbox         sandbox;
static FirmwareInnovator innovator(aiCtrl, cloudMgr, procStore, sandbox);

// ---------------------------------------------------------------------------
// Command dispatcher — handles commands from cloud, mobile, and Flipper
// ---------------------------------------------------------------------------
static void dispatchCommand(const std::string& topic, const std::string& payload) {
    ESP_LOGI(LOG_TAG_MAIN, "Command [%s]: %s", topic.c_str(), payload.c_str());

    // Parse incoming command JSON
    // Expected: {"action":"<verb>", ...}
    // Supported actions:
    //   "ping"       → reply with status
    //   "innovate"   → queue innovation task
    //   "flipper"    → relay command to Flipper Zero
    //   "ai_query"   → fire AI query and publish result
    //   "procedures" → publish procedure list
    //   "reboot"     → schedule device reboot

    JsonDocument cmdDoc;
    DeserializationError parseErr = deserializeJson(cmdDoc, payload.c_str());
    if (parseErr) {
        ESP_LOGW(LOG_TAG_MAIN, "Bad command JSON: %s", parseErr.c_str());
        return;
    }
    const char* action = cmdDoc["action"] | "";

    if (strcmp(action, "ping") == 0) {
        cloudMgr.publishStatus("{\"pong\":true,\"device\":\"" DEVICE_NAME "\"}");
        return;
    }
    if (strcmp(action, "innovate") == 0) {
        static uint32_t taskSeq = 0;
        InnovationTask t;
        t.id          = "task_" + std::to_string(++taskSeq);
        t.description = cmdDoc["description"] | "General firmware improvement";
        t.context     = payload;
        innovator.queueTask(t);
        return;
    }
    if (strcmp(action, "flipper") == 0) {
        flipper.sendCustom(payload);
        return;
    }
    if (strcmp(action, "ai_query") == 0) {
        std::string ctx = cmdDoc["context"] | "User query from cloud";
        AiResponse resp = aiCtrl.diagnose(ctx);
        cloudMgr.publishAiResult("{\"decision\":\"" + resp.decision + "\"}");
        return;
    }
    if (strcmp(action, "procedures") == 0) {
        cloudMgr.publishStatus(procStore.toJson());
        return;
    }
    if (strcmp(action, "reboot") == 0) {
        ESP_LOGW(LOG_TAG_MAIN, "Reboot requested via command");
        delay(500);
        ESP.restart();
        return;
    }
    ESP_LOGW(LOG_TAG_MAIN, "Unknown action '%s'", action);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    ESP_LOGI(LOG_TAG_MAIN, "=== " DEVICE_NAME " v" DEVICE_VERSION " booting ===");

    // 1. Cloud connectivity (WiFi + MQTT + OTA)
    cloudMgr.begin();
    cloudMgr.onCommand(dispatchCommand);

    // 2. Procedure storage (SPIFFS)
    procStore.begin();

    // 3. Flipper Zero bridge
    flipper.begin();
    flipper.onEvent([](const FlipperPacket& pkt) {
        ESP_LOGI(LOG_TAG_MAIN, "Flipper event cmd=0x%02X payload=%s",
                 static_cast<int>(pkt.cmd), pkt.payload.c_str());
        // Relay Flipper events to cloud
        cloudMgr.publishStatus(
            "{\"source\":\"flipper\",\"cmd\":" +
            std::to_string(static_cast<int>(pkt.cmd)) +
            ",\"payload\":" + pkt.payload + "}");
    });

    // 4. Mobile / PC REST + BLE API
    mobileApi.begin();
    mobileApi.onCommand([](const std::string& json) {
        dispatchCommand("mobile/cmd", json);
    });

    // 5. AI controller — publish decisions to cloud
    aiCtrl.onDecision([](const std::string& decision) {
        cloudMgr.publishAiResult("{\"decision\":\"" + decision + "\"}");
    });

    // 6. Firmware Innovator
    innovator.begin();
    innovator.onComplete([](const InnovationResult& r) {
        std::string msg = "{\"task\":\"" + r.taskId + "\","
                          "\"passed\":" + (r.passed ? "true" : "false") + ","
                          "\"procedure\":\"" + r.procedureName + "\","
                          "\"iterations\":" + std::to_string(r.iterations) + "}";
        cloudMgr.publishInnovatorResult(msg);
        mobileApi.notifyBle(msg);
    });

    ESP_LOGI(LOG_TAG_MAIN, "Boot complete. Free heap: %u bytes", ESP.getFreeHeap());
    cloudMgr.publishStatus("{\"status\":\"online\",\"device\":\"" DEVICE_NAME "\"}");
}

// ---------------------------------------------------------------------------
void loop() {
    cloudMgr.loop();    // MQTT keepalive + OTA
    flipper.loop();     // Read Flipper serial buffer
    mobileApi.loop();   // BLE / async HTTP (mostly no-op)
    innovator.tick();   // Run one step of the innovation loop if pending
}

#endif // NATIVE_TEST
