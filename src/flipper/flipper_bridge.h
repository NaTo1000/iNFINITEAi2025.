#pragma once
// =============================================================================
// flipper_bridge.h — Flipper Zero integration (UART + BLE)
//
// The Flipper Zero acts as the physical action interface:
//  • Receives command packets from the ESP32 and executes NFC/RF/GPIO actions
//  • Sends event/result packets back to the ESP32
//  • Also reachable over BLE for wireless proximity control
// =============================================================================
#include <string>
#include <functional>
#include "../config.h"

// Forward declarations
class CloudManager;

// Packet types exchanged with Flipper Zero
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
    FlipperCmd  cmd;
    std::string payload;  // JSON or raw hex data
};

using FlipperEventCallback = std::function<void(const FlipperPacket& pkt)>;

class FlipperBridge {
public:
    FlipperBridge();

    bool begin();
    void loop();

    // Send a command to the Flipper Zero
    bool send(const FlipperPacket& pkt);

    // Convenience senders
    bool ping();
    bool nfcEmulate(const std::string& uidHex);
    bool rfTransmit(const std::string& dataHex, uint32_t freq_hz = 433920000);
    bool gpioSet(uint8_t pin, bool state);
    bool irTransmit(const std::string& irSignalHex);
    bool sendCustom(const std::string& jsonPayload);

    // Register callback for events coming FROM the Flipper
    void onEvent(FlipperEventCallback cb) { _eventCb = cb; }

    bool isConnected() const { return _connected; }

private:
    bool          _connected = false;
    FlipperEventCallback _eventCb;
    std::string   _rxBuffer;

    void _processIncoming(const std::string& line);
    std::string _packetToJson(const FlipperPacket& pkt) const;
    FlipperPacket _jsonToPacket(const std::string& json) const;
};
