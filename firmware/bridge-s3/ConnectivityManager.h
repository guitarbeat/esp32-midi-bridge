#ifndef CONNECTIVITY_MANAGER_H
#define CONNECTIVITY_MANAGER_H

#include <Arduino.h>
#include "Transport.h"

/**
 * @brief The Connectivity Manager.
 * 
 * Deep Module: Encapsulates WiFi, Provisioning, and RTP-MIDI.
 * Hides the complex state machine of network setup from the coordinator.
 */
class ConnectivityManager : public Transport {
public:
    ConnectivityManager();

    void begin();
    void task() override;

    // Transport implementation
    const char* name() const override { return "RTP-MIDI"; }
    TransportKind kind() const override { return TransportKind::kRtp; }
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

extern ConnectivityManager connectivityManager;

#endif
