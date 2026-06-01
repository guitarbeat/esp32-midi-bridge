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
    struct RouteStats {
        uint32_t received = 0;
        uint32_t sent = 0;
        uint32_t skipped = 0;
        uint32_t failed = 0;
    };

    struct Counters {
        RouteStats transport[kTransportKindCount];
        uint32_t filteredActiveSense = 0;
        uint32_t filteredClock = 0;
        uint32_t filteredSysex = 0;
        uint32_t filteredOther = 0;
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
    const RouteStats& statsFor(TransportKind kind) const { return counters_.transport[transportKindIndex(kind)]; }

private:
    BridgeUi* ui_ = nullptr;
    std::function<bool()> isPaused_;
    MidiEngine* engine_ = nullptr;
    std::vector<Transport*> transports_;
    Counters counters_;

    void onMidiReceived(Transport* source, const uint8_t* data, size_t length);
    void notifyRouteUi(const uint8_t* data, size_t length, uint16_t color);
    RouteStats& statsForMutable(TransportKind kind) { return counters_.transport[transportKindIndex(kind)]; }
    bool isPaused() const;
};

extern MidiBridge midiBridge;

#endif
