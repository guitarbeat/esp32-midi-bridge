#include "MidiBridge.h"

#include "MidiCodec.h"
#include "MidiEngine.h"

class BridgeUi {
public:
    void notifyStatus(const char* text, uint16_t color);
};

MidiBridge midiBridge;

void MidiBridge::begin(BridgeUi* ui, std::function<bool()> isPaused)
{
    ui_ = ui;
    isPaused_ = std::move(isPaused);
}

void MidiBridge::setMidiEngine(MidiEngine* engine)
{
    engine_ = engine;
}

void MidiBridge::addTransport(Transport* transport)
{
    if (transport == nullptr) {
        return;
    }
    transports_.push_back(transport);
    transport->setReceiveCallback([this, transport](const uint8_t* data, size_t length) {
        this->onMidiReceived(transport, data, length);
    });
}

bool MidiBridge::isPaused() const
{
    return isPaused_ && isPaused_();
}

void MidiBridge::notifyRouteUi(const uint8_t* data, size_t length, uint16_t color)
{
    if (ui_ == nullptr || data == nullptr || length < 1) {
        return;
    }

    char logLine[32] = {0};
    if (MidiCodec::formatLogLine(data, length, logLine, sizeof(logLine))) {
        ui_->notifyStatus(logLine, color);
    }
}

MidiBridge::Result MidiBridge::route(Transport* source, const uint8_t* data, size_t length)
{
    if (source != nullptr && source->isPrimaryInbound()) {
        counters_.usbPacketsSeen++;
    }

    if (data == nullptr || length < 1) {
        return Result::kIgnored;
    }

    const uint8_t status = data[0];
    if (status == 0xFE || status == 0xF8) {
        return Result::kFiltered;
    }

    uint8_t rawMidi[256];
    const size_t processLen = length > sizeof(rawMidi) ? sizeof(rawMidi) : length;
    memcpy(rawMidi, data, processLen);

    if (engine_ != nullptr && (status >= 0x80 && status < 0xF0)) {
        if (!engine_->prepareOutbound(rawMidi, processLen)) {
            return Result::kFiltered;
        }
    }

    if (isPaused()) {
        notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteSkip);
        return Result::kFiltered;
    }

    bool forwardedPrimary = false;
    for (auto* t : transports_) {
        if (t == source) {
            continue;
        }

        if (!t->isConnected()) {
            if (t->isPrimaryOutbound()) {
                counters_.blePacketsSkipped++;
                notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteSkip);
            }
            continue;
        }

        if (t->sendMidi(rawMidi, processLen)) {
            if (t->isPrimaryOutbound()) {
                counters_.blePacketsSent++;
                notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteOk);
                forwardedPrimary = true;
            }
        } else if (t->isPrimaryOutbound()) {
            counters_.blePacketsSkipped++;
            notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteFail);
        }
    }

    return forwardedPrimary ? Result::kForwarded : Result::kFiltered;
}

void MidiBridge::onMidiReceived(Transport* source, const uint8_t* data, size_t length)
{
    route(source, data, length);
}
