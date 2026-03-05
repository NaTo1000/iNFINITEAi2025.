// =============================================================================
// flipper_bridge.cpp — Flipper Zero UART + BLE implementation
// =============================================================================
#include "flipper_bridge.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <esp_log.h>
static SoftwareSerial flipperSerial(FLIPPER_UART_RX, FLIPPER_UART_TX);
#else
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

FlipperBridge::FlipperBridge() {}

// -----------------------------------------------------------------------------
bool FlipperBridge::begin() {
#ifndef NATIVE_TEST
    flipperSerial.begin(FLIPPER_BAUD);
    ESP_LOGI(LOG_TAG_FLIPPER, "UART bridge ready (RX=%d TX=%d baud=%d)",
             FLIPPER_UART_RX, FLIPPER_UART_TX, FLIPPER_BAUD);
#endif
    // Send a PING to verify the Flipper is alive
    return ping();
}

// -----------------------------------------------------------------------------
void FlipperBridge::loop() {
#ifndef NATIVE_TEST
    while (flipperSerial.available()) {
        char c = flipperSerial.read();
        if (c == '\n') {
            if (!_rxBuffer.empty()) {
                _processIncoming(_rxBuffer);
                _rxBuffer.clear();
            }
        } else {
            _rxBuffer += c;
        }
    }
#endif
}

// -----------------------------------------------------------------------------
bool FlipperBridge::send(const FlipperPacket& pkt) {
    std::string json = _packetToJson(pkt);
    ESP_LOGI(LOG_TAG_FLIPPER, "→ Flipper: %s", json.c_str());
#ifndef NATIVE_TEST
    flipperSerial.println(json.c_str());
#endif
    return true;
}

// -----------------------------------------------------------------------------
bool FlipperBridge::ping() {
    FlipperPacket pkt{ FlipperCmd::PING, "{}" };
    return send(pkt);
}

bool FlipperBridge::nfcEmulate(const std::string& uidHex) {
    FlipperPacket pkt{ FlipperCmd::NFC_EMULATE,
                       "{\"uid\":\"" + uidHex + "\"}" };
    return send(pkt);
}

bool FlipperBridge::rfTransmit(const std::string& dataHex, uint32_t freq_hz) {
    FlipperPacket pkt{ FlipperCmd::RF_TRANSMIT,
                       "{\"data\":\"" + dataHex + "\","
                       "\"freq\":" + std::to_string(freq_hz) + "}" };
    return send(pkt);
}

bool FlipperBridge::gpioSet(uint8_t pin, bool state) {
    FlipperPacket pkt{ FlipperCmd::GPIO_SET,
                       "{\"pin\":" + std::to_string(pin) +
                       ",\"state\":" + (state ? "true" : "false") + "}" };
    return send(pkt);
}

bool FlipperBridge::irTransmit(const std::string& irSignalHex) {
    FlipperPacket pkt{ FlipperCmd::IR_TRANSMIT,
                       "{\"signal\":\"" + irSignalHex + "\"}" };
    return send(pkt);
}

bool FlipperBridge::sendCustom(const std::string& jsonPayload) {
    FlipperPacket pkt{ FlipperCmd::CUSTOM, jsonPayload };
    return send(pkt);
}

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------
void FlipperBridge::_processIncoming(const std::string& line) {
    ESP_LOGI(LOG_TAG_FLIPPER, "← Flipper: %s", line.c_str());
    FlipperPacket pkt = _jsonToPacket(line);
    if (pkt.cmd == FlipperCmd::ACK)  _connected = true;
    if (pkt.cmd == FlipperCmd::NACK) ESP_LOGW(LOG_TAG_FLIPPER, "Flipper NACK");
    if (_eventCb) _eventCb(pkt);
}

std::string FlipperBridge::_packetToJson(const FlipperPacket& pkt) const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["cmd"]     = static_cast<uint8_t>(pkt.cmd);
    doc["payload"] = pkt.payload.c_str();
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    return "{\"cmd\":" + std::to_string(static_cast<int>(pkt.cmd)) +
           ",\"payload\":" + pkt.payload + "}";
#endif
}

FlipperPacket FlipperBridge::_jsonToPacket(const std::string& json) const {
    FlipperPacket pkt{ FlipperCmd::ACK, "{}" };
#ifndef NATIVE_TEST
    JsonDocument doc;
    if (!deserializeJson(doc, json.c_str())) {
        pkt.cmd     = static_cast<FlipperCmd>(doc["cmd"].as<uint8_t>());
        pkt.payload = doc["payload"] | "{}";
    }
#endif
    return pkt;
}
