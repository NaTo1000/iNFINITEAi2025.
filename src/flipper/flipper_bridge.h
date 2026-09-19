#pragma once
// =============================================================================
// flipper_bridge.h — Flipper Zero UART bridge with validation and ACK handling
// =============================================================================
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../config.h"

enum class FlipperCmd : uint8_t {
    PING          = 0x01,
    NFC_EMULATE   = 0x10,
    RF_TRANSMIT   = 0x11,
    GPIO_SET      = 0x12,
    IR_TRANSMIT   = 0x13,
    CUSTOM        = 0x20,
    ACK           = 0xF0,
    NACK          = 0xF1,
};

struct FlipperPacket {
    FlipperCmd  cmd = FlipperCmd::PING;
    std::string payload;
};

using FlipperEventCallback = std::function<void(const FlipperPacket& pkt)>;

class FlipperBridge {
public:
    FlipperBridge();

    bool begin();
    void loop();
    bool send(const FlipperPacket& pkt);

    bool ping();
    bool nfcEmulate(const std::string& uidHex);
    bool rfTransmit(const std::string& dataHex, uint32_t freq_hz = 433920000U);
    bool gpioSet(uint8_t pin, bool state);
    bool irTransmit(const std::string& irSignalHex);
    bool sendCustom(const std::string& jsonPayload);

    void onEvent(FlipperEventCallback cb) { _eventCb = cb; }
    bool isConnected() const { return _connected; }
    const std::string& lastError() const { return _lastError; }

#ifdef NATIVE_TEST
    void injectIncomingLineForTest(const std::string& line);
    std::string lastTxForTest() const { return _lastTx; }
    bool processIncomingForTest(const std::string& line);
#endif

private:
    bool _connected = false;
    bool _awaitingAck = false;
    FlipperEventCallback _eventCb;
    std::string _rxBuffer;
    std::string _lastError;
#ifdef NATIVE_TEST
    std::vector<std::string> _pendingIncoming;
    std::string _lastTx;
#endif

    bool _awaitAck();
    bool _validatePacket(const FlipperPacket& pkt);
    bool _isHexPayload(const std::string& value) const;
    bool _isDangerousCommand(FlipperCmd cmd) const;
    bool _processIncoming(const std::string& line);
    std::string _packetToJson(const FlipperPacket& pkt) const;
    FlipperPacket _jsonToPacket(const std::string& json) const;
};
