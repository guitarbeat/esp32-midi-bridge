#ifndef BLE_CONNECTION_H
#define BLE_CONNECTION_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include "Transport.h"

// Standard BLE MIDI Service UUIDs (Apple/MIDI Association specification)
#define BLE_MIDI_SERVICE_UUID        "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHARACTERISTIC_UUID "7772E5DB-3868-4112-A1A9-F2669D106BF3"

// Structure to store a raw BLE packet (optional, useful for debugging).
struct RawBleMessage {
    uint8_t data[64];  // Full received buffer (max 64 bytes)
    size_t length;     // Actual number of bytes received
};

class BLEConnection : public Transport {
public:
    // Callback type for incoming MIDI messages
    typedef void (*MIDIMessageCallback)(const uint8_t* data, size_t length);

    BLEConnection();
    virtual ~BLEConnection();

    // Initializes the BLE MIDI server and starts advertising.
    // The deviceName parameter allows customizing the BLE device name.
    void begin(const std::string& deviceName = "ESP32 MIDI BLE");

    // Processes BLE events (generally not needed periodically).
    void task() override;

    // Transport implementation
    const char* name() const override { return "BLE-MIDI"; }
    TransportKind kind() const override { return TransportKind::kBle; }
    bool isConnected() const override;
    bool canSend() const override { return isSubscribed(); }
    bool isSubscribed() const { return subscribed_; }
    bool isPrimaryOutbound() const override { return true; }
    bool sendMidi(const uint8_t* data, size_t length) override;

    uint16_t getAverageLatencyMs() const { return avgLatencyMs_; }
    void recordForwardLatency(uint32_t latencyMs);

    // Registers a callback to handle incoming BLE MIDI messages.
    void setMidiMessageCallback(MIDIMessageCallback cb);

    void processIncomingBlePacket(const uint8_t* data, size_t length);

    // Virtual callback invoked when a BLE MIDI message (4 bytes) is received.
    // Upper layer or subclass should override this method.
    virtual void onMidiDataReceived(const uint8_t* data, size_t length);

protected:
    static constexpr uint8_t kIncomingQueueDepth = 16;

    BLEServer* pServer;
    BLECharacteristic* pCharacteristic;
    BLECharacteristicCallbacks* pBleCallback;
    BLEServerCallbacks* pServerCallback;
    SemaphoreHandle_t sendMutex;
    MIDIMessageCallback midiCallback;
    uint16_t avgLatencyMs_;
    volatile bool subscribed_;
    RawBleMessage incomingQueue_[kIncomingQueueDepth];
    volatile uint8_t incomingHead_;
    volatile uint8_t incomingTail_;
    portMUX_TYPE incomingMux_;

    bool enqueueIncomingMidi(const uint8_t* data, size_t length);
    bool dequeueIncomingMidi(RawBleMessage& message);
    void dispatchIncomingMidi(const uint8_t* data, size_t length);
    void setSubscribed(bool subscribed);
};

#endif // BLE_CONNECTION_H
