#ifndef MIDI_BRIDGE_H
#define MIDI_BRIDGE_H

#include <Arduino.h>

#include "RTPMidiConfig.h"
#include "Transport.h"
#include <vector>

class BridgeSettings;
class BridgeUi;
class MidiEngine;

class MidiBridge {
public:
    struct Counters {
        uint32_t usbPacketsSeen = 0;
        uint32_t blePacketsSent = 0;
    };

    enum class Result : uint8_t {
        kFiltered,
        kIgnored,
        kForwarded,
    };

    void begin(BridgeSettings* settings, BridgeUi* ui);
    void setMidiEngine(MidiEngine* engine);
    
    /** @brief Adds a transport to the bridge. The bridge will route messages to/from it. */
    void addTransport(Transport* transport);

    /** @brief Routes a MIDI packet from a source transport to all other connected transports. */
    Result route(Transport* source, const uint8_t* data, size_t length);

    const Counters& counters() const { return counters_; }

private:
    BridgeSettings* settings_ = nullptr;
    BridgeUi* ui_ = nullptr;
    MidiEngine* engine_ = nullptr;
    std::vector<Transport*> transports_;
    Counters counters_;

    void onMidiReceived(Transport* source, const uint8_t* data, size_t length);
};

extern MidiBridge midiBridge;

#endif
