#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <Arduino.h>
#include <functional>

enum class MidiTransportKind : uint8_t {
    kUsbHost = 0,
    kBle,
    kRtp,
    kUart,
    kCount,
};

constexpr size_t kTransportKindCount = static_cast<size_t>(MidiTransportKind::kCount);

inline size_t transportKindIndex(MidiTransportKind kind)
{
    return static_cast<size_t>(kind);
}

inline const char* transportKindShortName(MidiTransportKind kind)
{
    switch (kind) {
        case MidiTransportKind::kUsbHost: return "USB";
        case MidiTransportKind::kBle: return "BLE";
        case MidiTransportKind::kRtp: return "RTP";
        case MidiTransportKind::kUart: return "UART";
        default: return "?";
    }
}

/**
 * @brief The MidiTransport Seam.
 * 
 * Defines a generic interface for any MIDI connection (USB, BLE, RTP, etc.).
 * Deep Module Principle: Hides hardware-specific enumeration and transmission quirks.
 */
class MidiTransport {
public:
    using MidiReceiveCallback = std::function<void(const uint8_t* packet, size_t length)>;

    virtual ~MidiTransport() = default;

    /** @return A human-readable name for the transport (e.g., "USB-HOST", "BLE-MIDI"). */
    virtual const char* name() const = 0;

    /** @return Stable transport category used by routing diagnostics. */
    virtual MidiTransportKind kind() const = 0;

    /** @return True if the transport is physically/logically connected and ready. */
    virtual bool isConnected() const = 0;

    /** @return True if this transport can currently accept outbound MIDI. */
    virtual bool canSend() const { return isConnected(); }

    /** @brief Sends a MIDI packet out through this transport. */
    virtual bool sendMidi(const uint8_t* packet, size_t length) = 0;

    /** @brief Sets the callback for incoming MIDI data from this hardware. */
    void setReceiveCallback(MidiReceiveCallback cb) { receiveCallback_ = cb; }

    /** @brief Optional periodic processing. */
    virtual void task() {}

    /** @brief True for the transport that feeds the bridge (USB host). Drives inbound counters. */
    virtual bool isPrimaryInbound() const { return false; }

    /** @brief True for the main outbound target (BLE). Drives outbound counters and route log. */
    virtual bool isPrimaryOutbound() const { return false; }

protected:
    MidiReceiveCallback receiveCallback_ = nullptr;
};

#endif
