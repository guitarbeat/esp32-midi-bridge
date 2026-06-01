#include "MidiBridge.h"

#include "MidiCodec.h"
#include "MidiEngine.h"

#ifdef ARDUINO
#include "BridgeUi.h"
#else
class BridgeUi {
public:
    void notifyStatus(const char* text, uint16_t color);
};
#endif

namespace {

bool isChannelVoice(uint8_t status)
{
    return status >= 0x80 && status < 0xF0;
}

bool isShortSystemMessage(uint8_t status)
{
    switch (status) {
        case 0xF1:  // MIDI Time Code Quarter Frame
        case 0xF2:  // Song Position Pointer
        case 0xF3:  // Song Select
        case 0xF6:  // Tune Request
#if ENABLE_MIDI_CLOCK_PASSTHROUGH
        case 0xF8:  // Timing Clock
#endif
        case 0xFA:  // Start
        case 0xFB:  // Continue
        case 0xFC:  // Stop
            return true;
        default:
            return false;
    }
}

bool isAllowedShortMidi(uint8_t status)
{
    return isChannelVoice(status) || isShortSystemMessage(status);
}

}  // namespace

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
    if (data == nullptr || length < 1) {
        return Result::kIgnored;
    }

    if (source != nullptr) {
        statsForMutable(source->kind()).received++;
    }

    const uint8_t status = data[0];
    if (status == 0xFE) {
        counters_.filteredActiveSense++;
        return Result::kFiltered;
    }

#if !ENABLE_MIDI_CLOCK_PASSTHROUGH
    if (status == 0xF8) {
        counters_.filteredClock++;
        return Result::kFiltered;
    }
#endif

    if (status == 0xF0 || status == 0xF7) {
        counters_.filteredSysex++;
        return Result::kFiltered;
    }

    if (!isAllowedShortMidi(status)) {
        counters_.filteredOther++;
        return Result::kFiltered;
    }

    uint8_t rawMidi[256];
    const size_t processLen = length > sizeof(rawMidi) ? sizeof(rawMidi) : length;
    memcpy(rawMidi, data, processLen);

    if (engine_ != nullptr && isChannelVoice(status)) {
        if (!engine_->prepareOutbound(rawMidi, processLen)) {
            counters_.filteredOther++;
            return Result::kFiltered;
        }
    }

    if (isPaused()) {
        notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteSkip);
        return Result::kFiltered;
    }

    bool forwarded = false;
    bool failed = false;
    for (auto* t : transports_) {
        if (t == source) {
            continue;
        }

        RouteStats& targetStats = statsForMutable(t->kind());
        if (!t->canSend()) {
            targetStats.skipped++;
            continue;
        }

        if (t->sendMidi(rawMidi, processLen)) {
            targetStats.sent++;
            forwarded = true;
        } else {
            targetStats.failed++;
            failed = true;
        }
    }

    if (forwarded) {
        notifyRouteUi(rawMidi, processLen, BridgeColors::kRouteOk);
        return Result::kForwarded;
    }

    notifyRouteUi(rawMidi, processLen, failed ? BridgeColors::kRouteFail : BridgeColors::kRouteSkip);
    return Result::kFiltered;
}

void MidiBridge::onMidiReceived(Transport* source, const uint8_t* data, size_t length)
{
    route(source, data, length);
}
