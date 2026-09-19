// =============================================================================
// main.cpp — iNFINITEAi2025 ESP32 firmware entry point
// =============================================================================
#ifndef NATIVE_TEST

#include <Arduino.h>
#include <cstring>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <time.h>
#include <vector>

#include "config.h"
#include "cloud/cloud_manager.h"
#include "ai/ai_controller.h"
#include "flipper/flipper_bridge.h"
#include "mobile/mobile_api.h"
#include "innovator/sandbox.h"
#include "innovator/firmware_innovator.h"
#include "procedures/procedure_store.h"

static CloudManager cloudMgr;
static AiController aiCtrl(cloudMgr);
static FlipperBridge flipper;
static ProcedureStore procStore;
static Sandbox sandbox;
static FirmwareInnovator innovator(aiCtrl, cloudMgr, procStore, sandbox);
static MobileApi mobileApi(cloudMgr, aiCtrl, flipper, procStore, innovator);
static std::vector<std::string> recentNonces;

static bool isDangerousFlipperCmd(uint8_t cmd) {
    return cmd == static_cast<uint8_t>(FlipperCmd::NFC_EMULATE) ||
           cmd == static_cast<uint8_t>(FlipperCmd::RF_TRANSMIT) ||
           cmd == static_cast<uint8_t>(FlipperCmd::GPIO_SET) ||
           cmd == static_cast<uint8_t>(FlipperCmd::IR_TRANSMIT);
}

static bool validateNonce(JsonDocument& doc) {
    const char* nonce = doc["nonce"] | "";
    if (nonce[0] == '\0') {
        return false;
    }
    for (const std::string& existing : recentNonces) {
        if (existing == nonce) {
            return false;
        }
    }
    recentNonces.emplace_back(nonce);
    if (recentNonces.size() > COMMAND_MAX_NONCE_CACHE) {
        recentNonces.erase(recentNonces.begin());
    }
    if (doc["ts"].is<uint32_t>()) {
        const uint32_t ts = doc["ts"].as<uint32_t>();
        const time_t now = time(nullptr);
        if (now > 1000) {
            const uint32_t nowMs = static_cast<uint32_t>(now) * 1000U;
            if (ts > nowMs + COMMAND_MAX_SKEW_MS || nowMs > ts + COMMAND_MAX_SKEW_MS) {
                return false;
            }
        }
    }
    return true;
}

static void publishJsonStatus(const JsonDocument& doc) {
    std::string payload;
    serializeJson(doc, payload);
    cloudMgr.publishStatus(payload);
}

static void publishJsonAi(const JsonDocument& doc) {
    std::string payload;
    serializeJson(doc, payload);
    cloudMgr.publishAiResult(payload);
}

static void dispatchCommand(const std::string& topic, const std::string& payload) {
    JsonDocument cmdDoc;
    const DeserializationError parseErr = deserializeJson(cmdDoc, payload.c_str());
    if (parseErr || !cmdDoc.is<JsonObject>()) {
        ESP_LOGW(LOG_TAG_MAIN, "Rejected malformed command payload");
        return;
    }

    const char* action = cmdDoc["action"] | "";
    if (action[0] == '\0') {
        ESP_LOGW(LOG_TAG_MAIN, "Rejected command missing action");
        return;
    }

    const bool fromMqtt = topic == MQTT_TOPIC_CMD;
    const bool dangerous =
        strcmp(action, "reboot") == 0 ||
        (strcmp(action, "flipper") == 0 && isDangerousFlipperCmd(cmdDoc["cmd"] | 0));

    if (fromMqtt) {
        const char* auth = cmdDoc["auth"] | "";
        if (strcmp(auth, MQTT_COMMAND_TOKEN) != 0 || !validateNonce(cmdDoc)) {
            ESP_LOGW(LOG_TAG_MAIN, "Rejected MQTT command lacking valid auth/nonce");
            return;
        }
    }
    if (dangerous && !(cmdDoc["confirm"] | false)) {
        ESP_LOGW(LOG_TAG_MAIN, "Rejected dangerous command without confirm=true");
        return;
    }

    if (strcmp(action, "ping") == 0) {
        JsonDocument resp;
        resp["pong"] = true;
        resp["device"] = DEVICE_NAME;
        publishJsonStatus(resp);
        return;
    }
    if (strcmp(action, "innovate") == 0) {
        static uint32_t taskSeq = 0;
        InnovationTask task;
        task.id = "task_" + std::to_string(++taskSeq);
        task.description = cmdDoc["description"] | "General firmware improvement";
        task.context = payload;
        innovator.queueTask(task);
        return;
    }
    if (strcmp(action, "flipper") == 0) {
        const uint8_t cmd = cmdDoc["cmd"] | 0U;
        std::string payloadJson = "{}";
        if (cmdDoc["payload"].is<JsonObject>()) {
            serializeJson(cmdDoc["payload"], payloadJson);
        }
        if (!flipper.send({static_cast<FlipperCmd>(cmd), payloadJson})) {
            ESP_LOGW(LOG_TAG_MAIN, "Rejected Flipper command: %s", flipper.lastError().c_str());
        }
        return;
    }
    if (strcmp(action, "ai_query") == 0) {
        const std::string context = cmdDoc["context"] | "User query from controller";
        const AiResponse resp = aiCtrl.diagnose(context);
        JsonDocument result;
        result["success"] = resp.success;
        result["verdict"] = resp.verdict == AiVerdict::PROPOSE ? "PROPOSE" :
            resp.verdict == AiVerdict::PASS ? "PASS" :
            resp.verdict == AiVerdict::FAIL ? "FAIL" : "NONE";
        result["decision"] = resp.decision.c_str();
        result["reason"] = resp.reason.c_str();
        publishJsonAi(result);
        return;
    }
    if (strcmp(action, "procedures") == 0) {
        cloudMgr.publishStatus(procStore.toJson());
        return;
    }
    if (strcmp(action, "reboot") == 0) {
        ESP_LOGW(LOG_TAG_MAIN, "Reboot requested via authenticated command");
        delay(250);
        ESP.restart();
        return;
    }

    ESP_LOGW(LOG_TAG_MAIN, "Unknown action '%s'", action);
}

void setup() {
    Serial.begin(115200);
    ESP_LOGI(LOG_TAG_MAIN, "=== %s v%s booting ===", DEVICE_NAME, DEVICE_VERSION);

    cloudMgr.begin();
    cloudMgr.onCommand(dispatchCommand);
    procStore.begin();

    flipper.begin();
    flipper.onEvent([](const FlipperPacket& pkt) {
        JsonDocument event;
        event["source"] = "flipper";
        event["cmd"] = static_cast<uint8_t>(pkt.cmd);
        event["payload"] = pkt.payload.c_str();
        publishJsonStatus(event);
    });

    mobileApi.begin();
    mobileApi.onCommand([](const std::string& json) {
        dispatchCommand("mobile/cmd", json);
    });

    aiCtrl.onDecision([](const std::string& decision) {
        JsonDocument doc;
        doc["decision"] = decision.c_str();
        publishJsonAi(doc);
    });

    innovator.begin();
    innovator.onComplete([](const InnovationResult& r) {
        JsonDocument doc;
        doc["task"] = r.taskId.c_str();
        doc["passed"] = r.passed;
        doc["cancelled"] = r.cancelled;
        doc["procedure"] = r.procedureName.c_str();
        doc["iterations"] = r.iterations;
        doc["log"] = r.log.c_str();
        std::string msg;
        serializeJson(doc, msg);
        cloudMgr.publishInnovatorResult(msg);
        mobileApi.notifyBle(msg);
    });

    JsonDocument boot;
    boot["status"] = "online";
    boot["device"] = DEVICE_NAME;
    publishJsonStatus(boot);
}

void loop() {
    cloudMgr.loop();
    flipper.loop();
    mobileApi.loop();
    innovator.tick();
}

#endif // NATIVE_TEST
