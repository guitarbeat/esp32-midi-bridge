#include "MidiBridge.h"

#include "BridgeSystem.h"
#include "BridgeUi.h"
#include "MidiCodec.h"
#include "MidiEngine.h"

#if ENABLE_RTP_MIDI
#include "ConnectivityManager.h"
#endif

MidiBridge midiBridge;

void MidiBridge::begin(BridgeSettings* settings, BridgeUi* ui)
{
    settings_ = settings;
    ui_ = ui;
}

void MidiBridge::setMidiEngine(MidiEngine* engine)
{
    engine_ = engine;
}

void MidiBridge::addTransport(Transport* transport)
{
    if (transport == nullptr) return;
    transports_.push_back(transport);
    transport->setReceiveCallback([this, transport](const uint8_t* data, size_t length) {
        this->onMidiReceived(transport, data, length);
    });
}

MidiBridge::Result MidiBridge::route(Transport* source, const uint8_t* data, size_t length)
{
    if (data == nullptr || length < 1) return Result::kIgnored;

    const uint8_t status = data[0];

    // Central Filter: Save bandwidth for BLE and UI by dropping noisy messages
    // Active Sensing (0xFE) is sent every 300ms by many keyboards
    if (status == 0xFE) return Result::kFiltered;
    // Timing Clock (0xF8) is sent 24 times per quarter note (can be 50-100Hz+)
    // Only forward if we specifically need sync (currently we don't)
    if (status == 0xF8) return Result::kFiltered;

    // Use a stack buffer for processing to allow transformation
    uint8_t rawMidi[256];
    const size_t processLen = length > sizeof(rawMidi) ? sizeof(rawMidi) : length;
    memcpy(rawMidi, data, processLen);

    // 1. Live Processing (MidiEngine) - only for channel voice messages
    if (engine_ != nullptr && (status >= 0x80 && status < 0xF0)) {
        if (!engine_->processPacket(rawMidi, 3)) {
            return Result::kFiltered;
        }
    }

    // 2. UI Notification (Visual feedback)
    if (ui_ != nullptr) {
        // UI expects a 4-byte shim for standard messages
        uint8_t uiPacket[4] = {0, rawMidi[0], rawMidi[1], rawMidi[2]};
        ui_->notifyMidiEvent(uiPacket);
    }

    if (bridgeSystem.isPaused()) {
        return Result::kFiltered;
    }

    // 3. Hardware Routing
    for (auto* t : transports_) {
        if (t != source && t->isConnected()) {
            t->sendMidi(rawMidi, processLen);
            
            // Statistics
            if (strcmp(t->name(), "BLE-MIDI") == 0) {
                counters_.blePacketsSent++;
            }
        }
    }

    return Result::kForwarded;
}

void MidiBridge::onMidiReceived(Transport* source, const uint8_t* data, size_t length)
{
    // Counters
    if (source && strcmp(source->name(), "USB-HOST") == 0) {
        counters_.usbPacketsSeen++;
    }
    
    // Route it
    route(source, data, length);
}

MidiBridge::Result MidiBridge::forward(const uint8_t* data, size_t length, uint8_t outMidiPacket[4])
{
    // Legacy shim
    (void)outMidiPacket;
    // We assume 'data' here is 4-byte USB (CIN+Status+D1+D2)
    if (length >= 4) {
        onMidiReceived(nullptr, data + 1, 3);
    }
    return Result::kForwarded;
}
