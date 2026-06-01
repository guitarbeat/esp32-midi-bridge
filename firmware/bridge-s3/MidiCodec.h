#ifndef MIDI_CODEC_H
#define MIDI_CODEC_H

#include <Arduino.h>
#include <stddef.h>

namespace MidiCodec {

/**
 * @brief MIDI Message Length Lookup.
 * Returns the total message length (including status byte) for a given status byte.
 * Returns 0 for undefined or variable-length (SysEx) messages.
 */
inline uint8_t lengthFromStatus(uint8_t status)
{
    if (status < 0x80) return 0;
    if (status >= 0xF8) return 1; // Real-time

    switch (status & 0xF0) {
        case 0xC0: case 0xD0: return 2;
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 3;
        default: break;
    }

    switch (status) {
        case 0xF1: case 0xF3: return 2;
        case 0xF2: return 3;
        case 0xF6: case 0xF7: return 1;
        default: return 0;
    }
}

/**
 * @brief USB MIDI Code Index Number (CIN) to length.
 */
inline uint8_t lengthFromUsbCin(uint8_t cin, uint8_t status)
{
    (void)status;
    switch (cin & 0x0F) {
        case 0x2: case 0xC: case 0xD: return 2;
        case 0x3: case 0x4: case 0x7: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: return 3;
        case 0x5: case 0xF: return 1;
        case 0x6: return 2;
        default: return 0;
    }
}

/**
 * @brief Decode one 4-byte USB-MIDI event to raw MIDI bytes.
 * @param runningStatus Per-cable running status (16 entries); updated when a status byte is seen.
 */
inline bool decodeUsbEventToRaw(const uint8_t evt[4],
                                uint8_t runningStatus[16],
                                uint8_t* out,
                                size_t* outLen)
{
    if (evt == nullptr || out == nullptr || outLen == nullptr || runningStatus == nullptr) {
        return false;
    }
    if (evt[0] == 0) {
        return false;
    }

    const uint8_t cin = evt[0] & 0x0F;
    const uint8_t cable = (evt[0] >> 4) & 0x0F;
    uint8_t& rs = runningStatus[cable];

    if (cin >= 0x04 && cin <= 0x07) {
        return false;
    }

    auto emit = [&](uint8_t len) {
        *outLen = len;
        return true;
    };

    switch (cin) {
        case 0x8: case 0x9: case 0xA: case 0xB: case 0xE:
            if (evt[1] & 0x80) {
                rs = evt[1];
                out[0] = evt[1];
                out[1] = evt[2];
                out[2] = evt[3];
                return emit(3);
            }
            if (rs == 0) {
                return false;
            }
            out[0] = rs;
            out[1] = evt[2];
            out[2] = evt[3];
            return emit(3);

        case 0xC: case 0xD:
            if (evt[1] & 0x80) {
                rs = evt[1];
                out[0] = evt[1];
                out[1] = evt[2];
                return emit(2);
            }
            if (rs == 0) {
                return false;
            }
            out[0] = rs;
            out[1] = evt[2];
            return emit(2);

        case 0x2:
            if (!(evt[1] & 0x80)) {
                return false;
            }
            rs = 0;
            out[0] = evt[1];
            out[1] = evt[2];
            return emit(2);

        case 0x3:
            if (!(evt[1] & 0x80)) {
                return false;
            }
            rs = 0;
            out[0] = evt[1];
            out[1] = evt[2];
            out[2] = evt[3];
            return emit(3);

        case 0x5:
        case 0xF:
            if (evt[1] >= 0xF8 || evt[1] == 0xF6) {
                out[0] = evt[1];
                return emit(1);
            }
            return false;

        default:
            if (evt[1] & 0x80) {
                const uint8_t len = lengthFromStatus(evt[1]);
                if (len == 0 || len > 3) {
                    return false;
                }
                rs = evt[1];
                out[0] = evt[1];
                for (uint8_t i = 1; i < len; i++) {
                    out[i] = evt[1 + i];
                }
                return emit(len);
            }
            return false;
    }
}

/**
 * @brief A stateful parser for MIDI byte streams (UART, Serial, BLE).
 * Deep Module Principle: Encapsulates the complexity of Running Status and SysEx.
 */
class Parser {
public:
    /** 
     * @brief Callback signature for decoded MIDI messages. 
     * @param status The MIDI status byte.
     * @param data Pointer to the data bytes (nullptr if none).
     * @param length Number of data bytes.
     * @param sysexPos For SysEx, the offset of this chunk from the start. 0 on start.
     * @param arg User argument.
     */
    typedef void (*Callback)(uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg);

    Parser(Callback cb = nullptr, void* arg = nullptr) 
        : cb_(cb), arg_(arg), runningStatus_(0), expectedData_(0), waitData_(0), bufferPos_(0), inSysEx_(false), sysexPos_(0) {}

    /** @brief Process a single byte from the stream. */
    void parse(uint8_t byte) {
        if (byte >= 0xF8) {
            // Real-time messages (Timing Clock, Active Sensing, etc.)
            // These do NOT affect running status.
            if (cb_) cb_(byte, nullptr, 0, 0, arg_);
            return;
        }

        if (byte & 0x80) { // Status byte
            runningStatus_ = byte;
            bufferPos_ = 0;

            if (byte == 0xF0) { // SysEx Start
                inSysEx_ = true;
                sysexPos_ = 0;
                if (cb_) cb_(byte, nullptr, 0, 0, arg_);
            } else if (byte == 0xF7) { // SysEx End
                inSysEx_ = false;
                if (cb_) cb_(byte, nullptr, 0, sysexPos_, arg_);
                runningStatus_ = 0; // System Common cancels running status
            } else {
                inSysEx_ = false;
                const uint8_t len = lengthFromStatus(byte);
                if (len > 0) {
                    expectedData_ = len - 1;
                    waitData_ = expectedData_;
                    if (expectedData_ == 0 && cb_) {
                        cb_(byte, nullptr, 0, 0, arg_);
                    }
                } else {
                    runningStatus_ = 0; // Invalid/Unknown status
                }
            }
        } else { // Data byte
            if (inSysEx_) {
                // Forward SysEx data byte immediately
                if (cb_) cb_(0xF0, &byte, 1, sysexPos_, arg_);
                sysexPos_++;
            } else if (runningStatus_) {
                if (waitData_ > 0) {
                    buffer_[bufferPos_++] = byte;
                    waitData_--;
                }

                if (waitData_ == 0) {
                    if (cb_) cb_(runningStatus_, buffer_, expectedData_, 0, arg_);
                    // Handle Running Status: reset for next data group
                    waitData_ = expectedData_;
                    bufferPos_ = 0;
                }
            }
        }
    }

    /** @brief Process a buffer of bytes from the stream. */
    void parse(const uint8_t* data, size_t length) {
        if (data == nullptr) return;
        for (size_t i = 0; i < length; i++) {
            parse(data[i]);
        }
    }

private:
    Callback cb_;
    void* arg_;
    uint8_t runningStatus_;
    uint8_t expectedData_;
    uint8_t waitData_;
    uint8_t buffer_[2];
    size_t bufferPos_;
    bool inSysEx_;
    size_t sysexPos_;
};

inline void appendBleTimestamp(uint8_t* packet, size_t* length, uint16_t timestampMs)
{
    const uint16_t timestamp = timestampMs & 0x1FFF;
    packet[0] = 0x80 | ((timestamp >> 7) & 0x3F);
    packet[1] = 0x80 | (timestamp & 0x7F);
    *length = 2;
}

inline bool buildBlePacket(const uint8_t* rawMidi,
                           size_t length,
                           uint16_t timestampMs,
                           uint8_t* blePacket,
                           size_t* bleLength)
{
    if (rawMidi == nullptr || blePacket == nullptr || bleLength == nullptr || length == 0) {
        return false;
    }

    appendBleTimestamp(blePacket, bleLength, timestampMs);

    for (size_t i = 0; i < length && (*bleLength + i) < 256; i++) {
        blePacket[2 + i] = rawMidi[i];
    }

    *bleLength = 2 + length;
    return true;
}

inline bool buildBlePacketFromUsb(const uint8_t* usbMidiPacket,
                                  size_t usbLength,
                                  uint16_t timestampMs,
                                  uint8_t* blePacket,
                                  size_t* bleLength)
{
    if (usbLength < 4 || usbMidiPacket == nullptr || blePacket == nullptr || bleLength == nullptr) {
        return false;
    }

    const uint8_t cin = usbMidiPacket[0] & 0x0F;
    const uint8_t status = usbMidiPacket[1];
    if (!(status & 0x80)) return false;

    const uint8_t midiLength = lengthFromUsbCin(cin, status);
    if (midiLength == 0 || midiLength > 3 || status == 0x00 || status == 0xFE) {
        return false;
    }

    appendBleTimestamp(blePacket, bleLength, timestampMs);
    for (uint8_t i = 0; i < midiLength; i++) {
        blePacket[2 + i] = usbMidiPacket[1 + i];
    }
    *bleLength = 2 + midiLength;
    return true;
}

inline const char* statusName(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: return "NoteOff";
        case 0x90: return "NoteOn";
        case 0xA0: return "PolyPressure";
        case 0xB0: return "ControlChange";
        case 0xC0: return "ProgramChange";
        case 0xD0: return "ChannelPressure";
        case 0xE0: return "PitchBend";
        default: return "System";
    }
}

inline const char* noteName(uint8_t note, char* buffer, size_t bufferLength)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = (note / 12) - 1;
    snprintf(buffer, bufferLength, "%s%d", names[note % 12], octave);
    return buffer;
}

/** @brief One-line MIDI log text from a raw MIDI packet (status at data[0]). */
inline bool formatLogLine(const uint8_t* data, size_t length, char* logLine, size_t logLineLength)
{
    if (data == nullptr || logLine == nullptr || logLineLength == 0 || length < 1) {
        return false;
    }

    const uint8_t status = data[0];
    const uint8_t messageType = status & 0xF0;

    if (messageType == 0x90 && length >= 3) {
        const bool noteOn = data[2] > 0;
        char noteBuffer[12] = {0};
        noteName(data[1], noteBuffer, sizeof(noteBuffer));
        snprintf(logLine, logLineLength, "%s %s v%u", noteOn ? "ON" : "OFF", noteBuffer, data[2]);
    } else if (messageType == 0x80 && length >= 3) {
        char noteBuffer[12] = {0};
        noteName(data[1], noteBuffer, sizeof(noteBuffer));
        snprintf(logLine, logLineLength, "OFF %s", noteBuffer);
    } else if (messageType == 0xB0 && length >= 3) {
        snprintf(logLine, logLineLength, "CC %u=%u", data[1], data[2]);
    } else {
        snprintf(logLine, logLineLength, "%s", statusName(status));
    }
    return true;
}

}  // namespace MidiCodec

#endif

