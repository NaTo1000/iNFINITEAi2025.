// =============================================================================
// flipper_bridge.cpp — Flipper Zero UART implementation
// =============================================================================
#include "flipper_bridge.h"
#include "../common/native_json.h"
#include <cctype>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <esp_log.h>
static SoftwareSerial flipperSerial(FLIPPER_UART_RX, FLIPPER_UART_TX);
#else
#include <cstdio>
#include <thread>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

FlipperBridge::FlipperBridge() = default;

bool FlipperBridge::begin() {
#ifndef NATIVE_TEST
    flipperSerial.begin(FLIPPER_BAUD);
    ESP_LOGI(LOG_TAG_FLIPPER, "UART bridge ready (RX=%d TX=%d baud=%d)",
             FLIPPER_UART_RX, FLIPPER_UART_TX, FLIPPER_BAUD);
#endif
    _connected = false;
    _lastError.clear();
    return ping() && _awaitAck();
}

void FlipperBridge::loop() {
#ifndef NATIVE_TEST
    while (flipperSerial.available()) {
        const char c = static_cast<char>(flipperSerial.read());
        if (c == '\n') {
            if (!_rxBuffer.empty()) {
                _processIncoming(_rxBuffer);
                _rxBuffer.clear();
            }
        } else if (_rxBuffer.size() < FLIPPER_MAX_PACKET_BYTES) {
            _rxBuffer += c;
        } else {
            _lastError = "incoming Flipper frame exceeded size limit";
            _rxBuffer.clear();
        }
    }
#else
    while (!_pendingIncoming.empty()) {
        const std::string line = _pendingIncoming.front();
        _pendingIncoming.erase(_pendingIncoming.begin());
        _processIncoming(line);
    }
#endif
}

bool FlipperBridge::send(const FlipperPacket& pkt) {
    if (!_validatePacket(pkt)) {
        return false;
    }
    const std::string json = _packetToJson(pkt);
    if (json.size() > FLIPPER_MAX_PACKET_BYTES) {
        _lastError = "encoded Flipper frame exceeded size limit";
        return false;
    }
    ESP_LOGI(LOG_TAG_FLIPPER, "Sending Flipper packet cmd=0x%02X", static_cast<int>(pkt.cmd));
#ifndef NATIVE_TEST
    flipperSerial.println(json.c_str());
#else
    _lastTx = json;
#endif
    return true;
}

bool FlipperBridge::ping() {
    _awaitingAck = true;
    _connected = false;
    return send({FlipperCmd::PING, "{}"});
}

bool FlipperBridge::nfcEmulate(const std::string& uidHex) {
    return send({FlipperCmd::NFC_EMULATE, std::string("{\"uid\":\"") + uidHex + "\"}"});
}

bool FlipperBridge::rfTransmit(const std::string& dataHex, uint32_t freq_hz) {
    return send({FlipperCmd::RF_TRANSMIT,
                 std::string("{\"data\":\"") + dataHex + "\",\"freq\":" + std::to_string(freq_hz) + "}"});
}

bool FlipperBridge::gpioSet(uint8_t pin, bool state) {
    return send({FlipperCmd::GPIO_SET,
                 std::string("{\"pin\":") + std::to_string(pin) + ",\"state\":" + (state ? "true" : "false") + "}"});
}

bool FlipperBridge::irTransmit(const std::string& irSignalHex) {
    return send({FlipperCmd::IR_TRANSMIT, std::string("{\"signal\":\"") + irSignalHex + "\"}"});
}

bool FlipperBridge::sendCustom(const std::string& jsonPayload) {
    return send({FlipperCmd::CUSTOM, jsonPayload});
}

bool FlipperBridge::_awaitAck() {
#ifndef NATIVE_TEST
    const unsigned long start = millis();
    while (_awaitingAck && millis() - start < FLIPPER_TIMEOUT_MS) {
        loop();
        delay(10);
    }
#else
    for (size_t i = 0; i < 32U && _awaitingAck; ++i) {
        loop();
        std::this_thread::yield();
    }
#endif
    if (_awaitingAck) {
        _lastError = "ping timed out waiting for ACK";
        _connected = false;
        return false;
    }
    return _connected;
}

bool FlipperBridge::_validatePacket(const FlipperPacket& pkt) {
    _lastError.clear();
    if (pkt.payload.empty() || pkt.payload.size() > FLIPPER_MAX_PACKET_BYTES) {
        _lastError = "Flipper payload is empty or too large";
        return false;
    }
    if (!nativejson::looksLikeJsonObject(pkt.payload) && !_isDangerousCommand(pkt.cmd)) {
        _lastError = "Flipper payload must be a JSON object";
        return false;
    }
    switch (pkt.cmd) {
        case FlipperCmd::PING:
        case FlipperCmd::ACK:
        case FlipperCmd::NACK:
            return pkt.payload == "{}" || nativejson::looksLikeJsonObject(pkt.payload);
        case FlipperCmd::NFC_EMULATE: {
            std::string uid;
            if (!nativejson::extractStringField(pkt.payload, "uid", uid) || !_isHexPayload(uid) || uid.size() > 2U * FLIPPER_MAX_HEX_BYTES) {
                _lastError = "Invalid NFC UID payload";
                return false;
            }
            return true;
        }
        case FlipperCmd::RF_TRANSMIT: {
            std::string data;
            uint32_t freq = 0;
            if (!nativejson::extractStringField(pkt.payload, "data", data) ||
                !nativejson::extractUIntField(pkt.payload, "freq", freq) ||
                !_isHexPayload(data) || freq < FLIPPER_MIN_RF_HZ || freq > FLIPPER_MAX_RF_HZ) {
                _lastError = "Invalid RF transmit payload";
                return false;
            }
            return true;
        }
        case FlipperCmd::GPIO_SET: {
            uint32_t pin = 0;
            bool state = false;
            if (!nativejson::extractUIntField(pkt.payload, "pin", pin) ||
                !nativejson::extractBoolField(pkt.payload, "state", state) || pin > 39U) {
                (void)state;
                _lastError = "Invalid GPIO payload";
                return false;
            }
            return true;
        }
        case FlipperCmd::IR_TRANSMIT: {
            std::string signal;
            if (!nativejson::extractStringField(pkt.payload, "signal", signal) || !_isHexPayload(signal)) {
                _lastError = "Invalid IR payload";
                return false;
            }
            return true;
        }
        case FlipperCmd::CUSTOM:
            if (!nativejson::looksLikeJsonObject(pkt.payload)) {
                _lastError = "Custom Flipper payload must be a JSON object";
                return false;
            }
            return true;
    }
    _lastError = "Unsupported Flipper command";
    return false;
}

bool FlipperBridge::_isHexPayload(const std::string& value) const {
    if (value.empty() || value.size() % 2U != 0U || value.size() > 2U * FLIPPER_MAX_HEX_BYTES) {
        return false;
    }
    for (char ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

bool FlipperBridge::_isDangerousCommand(FlipperCmd cmd) const {
    return cmd == FlipperCmd::NFC_EMULATE || cmd == FlipperCmd::RF_TRANSMIT ||
           cmd == FlipperCmd::GPIO_SET || cmd == FlipperCmd::IR_TRANSMIT;
}

bool FlipperBridge::_processIncoming(const std::string& line) {
    if (line.empty() || line.size() > FLIPPER_MAX_PACKET_BYTES) {
        _lastError = "incoming Flipper frame was empty or too large";
        return false;
    }
    const FlipperPacket pkt = _jsonToPacket(line);
    if (!nativejson::looksLikeJsonObject(pkt.payload) && pkt.payload != "{}") {
        _lastError = "incoming Flipper payload was malformed";
        return false;
    }
    if (pkt.cmd == FlipperCmd::ACK) {
        _connected = true;
        _awaitingAck = false;
    } else if (pkt.cmd == FlipperCmd::NACK) {
        _connected = false;
        _awaitingAck = false;
        _lastError = "Flipper returned NACK";
    }
    if (_eventCb) {
        _eventCb(pkt);
    }
    return true;
}

std::string FlipperBridge::_packetToJson(const FlipperPacket& pkt) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["cmd"] = static_cast<uint8_t>(pkt.cmd);
    JsonDocument payloadDoc;
    if (!deserializeJson(payloadDoc, pkt.payload.c_str()) && payloadDoc.is<JsonObject>()) {
        doc["payload"] = payloadDoc.as<JsonObject>();
    } else {
        doc["payload"] = pkt.payload.c_str();
    }
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    return std::string("{\"cmd\":") + std::to_string(static_cast<int>(pkt.cmd)) +
           ",\"payload\":" + pkt.payload + "}";
#endif
}

FlipperPacket FlipperBridge::_jsonToPacket(const std::string& json) const {
    FlipperPacket pkt{};
#ifndef NATIVE_TEST
    JsonDocument doc;
    if (deserializeJson(doc, json.c_str())) {
        return pkt;
    }
    pkt.cmd = static_cast<FlipperCmd>(doc["cmd"] | static_cast<uint8_t>(FlipperCmd::NACK));
    std::string payload;
    if (doc["payload"].is<JsonObject>()) {
        serializeJson(doc["payload"], payload);
        pkt.payload = payload;
    } else {
        pkt.payload = doc["payload"] | "{}";
    }
#else
    uint32_t cmdValue = static_cast<uint32_t>(FlipperCmd::NACK);
    nativejson::extractUIntField(json, "cmd", cmdValue);
    pkt.cmd = static_cast<FlipperCmd>(cmdValue);
    std::string payloadJson;
    if (nativejson::extractObjectField(json, "payload", payloadJson)) {
        pkt.payload = payloadJson;
    } else {
        pkt.payload = "{}";
    }
#endif
    return pkt;
}

#ifdef NATIVE_TEST
void FlipperBridge::injectIncomingLineForTest(const std::string& line) {
    _pendingIncoming.push_back(line);
}

bool FlipperBridge::processIncomingForTest(const std::string& line) {
    return _processIncoming(line);
}
#endif
