#include "MidiBridge.h"

#include "BridgeSettings.h"
#include "BridgeUi.h"
#include "MidiCodec.h"

#if ENABLE_RTP_MIDI
#include "NetworkServices.h"
#endif

MidiBridge midiBridge;

void MidiBridge::begin(BridgeSettings* settings, BridgeUi* ui)
{
    settings_ = settings;
    ui_ = ui;
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
    if (data == nullptr || length < 3) return Result::kIgnored;

    // Use a work buffer to allow transformation
    uint8_t outMidiPacket[4] = {0, data[0], data[1], data[2]};

    // Apply filters/transformations if needed (this will move to MidiEngine later)
    if (settings_ != nullptr) {
        // Simple shim for now
        // outMidiPacket[1] is status, outMidiPacket[2] is data1, outMidiPacket[3] is data2
        // Wait, the existing code uses 4-byte packets.
    }

    if (ui_ != nullptr) {
        ui_->notifyMidiEvent(outMidiPacket);
    }

    if (ui_ != nullptr && ui_->isBridgePaused()) {
        return Result::kFiltered;
    }

    for (auto* t : transports_) {
        if (t != source && t->isConnected()) {
            t->sendMidi(data, length);
        }
    }

    return Result::kForwarded;
}

void MidiBridge::onMidiReceived(Transport* source, const uint8_t* data, size_t length)
{
    if (source && strcmp(source->name(), "USB-HOST") == 0) {
        counters_.usbPacketsSeen++;
    }
    route(source, data, length);
}

MidiBridge::Result MidiBridge::forward(const uint8_t* data, size_t length, uint8_t outMidiPacket[4])
{
    // Legacy shim for transition
    (void)outMidiPacket;
    onMidiReceived(nullptr, data, length);
    return Result::kForwarded;
}
