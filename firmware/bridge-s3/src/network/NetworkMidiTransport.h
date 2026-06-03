#ifndef CONNECTIVITY_MANAGER_H
#define CONNECTIVITY_MANAGER_H

#include <Arduino.h>
#include "../midi/MidiTransport.h"

/**
 * @brief The Connectivity Manager.
 * 
 * Deep Module: Encapsulates WiFi, Provisioning, and RTP-MIDI.
 * Hides the complex state machine of network setup from the coordinator.
 */
class NetworkMidiTransport : public MidiTransport {
public:
    NetworkMidiTransport();

    void begin();
    void task() override;

    // MidiTransport implementation
    const char* name() const override { return "RTP-MIDI"; }
    MidiTransportKind kind() const override { return MidiTransportKind::kRtp; }
    bool isConnected() const override;
    bool canSend() const override { return isConnected(); }
    bool sendMidi(const uint8_t* packet, size_t length) override;

    bool hasRtpSession() const;
    void startProvisioning();

private:
    bool wifiConnected_ = false;
    bool rtpActive_ = false;
    uint32_t lastCheckMs_ = 0;

    void handleWifi();
    void handleRtp();
};

extern NetworkMidiTransport networkMidi;

#endif
