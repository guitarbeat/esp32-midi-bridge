#ifndef MIDI_BRIDGE_H
#define MIDI_BRIDGE_H

#include <Arduino.h>

#include "RTPMidiConfig.h"
#include "Transport.h"
#include <functional>
#include <vector>

class BridgeUi;
class MidiEngine;

namespace BridgeColors {
constexpr uint16_t kRouteOk = 0x07E0;    // lime
constexpr uint16_t kRouteSkip = 0xFD20;  // orange
constexpr uint16_t kRouteFail = 0xF800;  // red
}  // namespace BridgeColors

class MidiBridge {
public:
    struct Counters {
        uint32_t usbPacketsSeen = 0;
        uint32_t blePacketsSent = 0;
        uint32_t blePacketsSkipped = 0;
    };

    enum class Result : uint8_t {
        kFiltered,
        kIgnored,
        kForwarded,
    };

    void begin(BridgeUi* ui, std::function<bool()> isPaused = nullptr);
    void setMidiEngine(MidiEngine* engine);

    /** @brief Adds a transport to the bridge. The bridge will route messages to/from it. */
    void addTransport(Transport* transport);

    /** @brief Routes a MIDI packet from a source transport to all other connected transports. */
    Result route(Transport* source, const uint8_t* data, size_t length);

    const Counters& counters() const { return counters_; }

private:
    BridgeUi* ui_ = nullptr;
    std::function<bool()> isPaused_;
    MidiEngine* engine_ = nullptr;
    std::vector<Transport*> transports_;
    Counters counters_;

    void onMidiReceived(Transport* source, const uint8_t* data, size_t length);
    void notifyRouteUi(const uint8_t* data, size_t length, uint16_t color);
    bool isPaused() const;
};

extern MidiBridge midiBridge;

#endif
