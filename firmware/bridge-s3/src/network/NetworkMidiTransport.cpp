#include "NetworkMidiTransport.h"
#include "../config/NetworkConfig.h"

#if ENABLE_RTP_MIDI
#include "RtpMidiSession.h"
#include "WifiProvisioning.h"
#endif

#if ENABLE_OTA
#include "OtaUpdate.h"
#endif

NetworkMidiTransport networkMidi;

#if ENABLE_RTP_MIDI
static RtpMidiSession g_rtp;
#endif

NetworkMidiTransport::NetworkMidiTransport() {}

void NetworkMidiTransport::begin()
{
#if ENABLE_RTP_MIDI
    g_rtp.setReceiveCallback([this](const uint8_t* data, size_t length) {
        if (receiveCallback_ != nullptr) {
            receiveCallback_(data, length);
        }
    });
    g_rtp.begin("ESP32-MIDI-STATION");
#if ENABLE_OTA
    otaUpdate.begin("esp32-midi-bridge");
#endif
#endif
}

void NetworkMidiTransport::task()
{
#if ENABLE_RTP_MIDI
    g_rtp.task();
#if ENABLE_OTA
    otaUpdate.task(g_rtp.isWifiConnected());
#endif
#endif
}

bool NetworkMidiTransport::isConnected() const
{
#if ENABLE_RTP_MIDI
    return g_rtp.hasRtpSession();
#else
    return false;
#endif
}

bool NetworkMidiTransport::sendMidi(const uint8_t* packet, size_t length)
{
#if ENABLE_RTP_MIDI
    if (g_rtp.hasRtpSession()) {
        g_rtp.sendRawMidi(packet, length);
        return true;
    }
#endif
    return false;
}

bool NetworkMidiTransport::hasRtpSession() const
{
#if ENABLE_RTP_MIDI
    return g_rtp.hasRtpSession();
#else
    return false;
#endif
}

void NetworkMidiTransport::startProvisioning()
{
#if ENABLE_RTP_MIDI
    wifiProvisioning.enterSetupMode();
#endif
}
