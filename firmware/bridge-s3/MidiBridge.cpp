#include "MidiBridge.h"

#include "BridgeSettings.h"
#include "BridgeUi.h"
#include "MidiCodec.h"
#include "MidiEngine.h"

#if ENABLE_RTP_MIDI
#include "NetworkServices.h"
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
    if (data == nullptr || length < 3) return Result::kIgnored;

    // Use a work buffer to allow transformation
    uint8_t workPacket[4] = {0, data[0], data[1], data[2]};

    // Feed the MIDI Engine directly (Inverted Data Flow)
    if (engine_ != nullptr) {
        if (!engine_->processPacket(workPacket, 4)) {
            return Result::kFiltered;
        }
    }

    // Notify UI for logging/display
    if (ui_ != nullptr) {
        ui_->notifyMidiEvent(workPacket);
    }

    if (ui_ != nullptr && ui_->isBridgePaused()) {
        return Result::kFiltered;
    }

    for (auto* t : transports_) {
        if (t != source && t->isConnected()) {
            t->sendMidi(workPacket + 1, length);
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
    (void)outMidiPacket;
    onMidiReceived(nullptr, data, length);
    return Result::kForwarded;
}
