#ifndef MIDI_ENGINE_H
#define MIDI_ENGINE_H

#include <Arduino.h>
#include <stdint.h>

/**
 * @brief The Deep MIDI Engine.
 * 
 * High Locality: Centralizes note tracking, transposition, and filtering.
 * High Leverage: Callers (like UI) query state without knowing MIDI status byte details.
 */
class MidiEngine {
public:
    struct State {
        bool heldNotes[128] = {false};
        uint8_t heldCount = 0;
        uint8_t lastNote = 0;
        uint8_t lastVelocity = 0;
        bool sustainDown = false;
        char lastNoteLabel[12] = "--";
        uint32_t noteEventsSeen = 0;
        uint16_t notesPerMinute = 0;
    };

    MidiEngine();

    /** @brief Processes an incoming raw MIDI packet. Applies filters and updates state. */
    bool processPacket(uint8_t* packet, size_t length);

    /** @brief Sets the transposition amount in semitones. */
    void setTranspose(int8_t semitones) { transpose_ = semitones; }
    int8_t transpose() const { return transpose_; }

    /** @brief Sets the MIDI channel filter (0 = all). */
    void setChannelFilter(uint8_t channel) { channelFilter_ = channel; }
    uint8_t channelFilter() const { return channelFilter_; }

    /** @brief Returns the current tracked state of the MIDI stream. */
    const State& state() const { return state_; }

    void tick(uint32_t nowMs);

private:
    State state_;
    int8_t transpose_ = 0;
    uint8_t channelFilter_ = 0; // 0 = all

    uint32_t noteOnTimes[32] = {0};
    uint8_t noteOnHead_ = 0;

    void updateNoteLabel();
    void updateNotesPerMinute();
    uint8_t clampNote(int note);
};

extern MidiEngine midiEngine;

#endif
