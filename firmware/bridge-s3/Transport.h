#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <Arduino.h>
#include <functional>

/**
 * @brief The Transport Seam.
 * 
 * Defines a generic interface for any MIDI connection (USB, BLE, RTP, etc.).
 * Deep Module Principle: Hides hardware-specific enumeration and transmission quirks.
 */
class Transport {
public:
    using MidiReceiveCallback = std::function<void(const uint8_t* packet, size_t length)>;

    virtual ~Transport() = default;

    /** @return A human-readable name for the transport (e.g., "USB-HOST", "BLE-MIDI"). */
    virtual const char* name() const = 0;

    /** @return True if the transport is physically/logically connected and ready. */
    virtual bool isConnected() const = 0;

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
