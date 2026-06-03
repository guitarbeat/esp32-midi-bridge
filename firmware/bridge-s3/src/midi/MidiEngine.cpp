#include "MidiEngine.h"
#include "MidiCodec.h"

MidiEngine::MidiEngine()
{
    memset(&state_, 0, sizeof(state_));
    strcpy(state_.lastNoteLabel, "--");
}

bool MidiEngine::prepareOutbound(uint8_t* packet, size_t length)
{
    if (packet == nullptr || length < 1) {
        return false;
    }

    const uint8_t status = packet[0];
    if (!(status & 0x80)) {
        return false;
    }

    const uint8_t expectedLen = MidiCodec::lengthFromStatus(status);
    if (expectedLen == 0 || length < expectedLen) {
        return false;
    }

    const uint8_t msgType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;

    if (channelFilter_ > 0 && channel != channelFilter_) {
        return false;
    }

    if (msgType == 0x90 || msgType == 0x80) {
        if (length < 3) {
            return false;
        }

        const bool noteOn = (msgType == 0x90 && packet[2] > 0);
        const uint8_t originalNote = packet[1];

        if (transpose_ != 0) {
            packet[1] = clampNote(static_cast<int>(originalNote) + transpose_);
        }

        const uint8_t note = packet[1];

        if (noteOn) {
            if (!state_.heldNotes[note]) {
                state_.heldNotes[note] = true;
                state_.heldCount++;
            }
            state_.lastNote = note;
            state_.lastVelocity = packet[2];
            state_.noteEventsSeen++;
            noteOnTimes[noteOnHead_ % 32] = millis();
            noteOnHead_++;
            updateNoteLabel();
        } else if (state_.heldNotes[note]) {
            state_.heldNotes[note] = false;
            if (state_.heldCount > 0) {
                state_.heldCount--;
            }
        }
    } else if (msgType == 0xB0 && length >= 3) {
        if (packet[1] == 64) {
            state_.sustainDown = (packet[2] >= 64);
        }
    }

    return true;
}

void MidiEngine::tick(uint32_t nowMs)
{
    static uint32_t lastUpdate = 0;
    if (nowMs - lastUpdate > 1000) {
        updateNotesPerMinute();
        lastUpdate = nowMs;
    }
}

void MidiEngine::updateNoteLabel()
{
    MidiCodec::noteName(state_.lastNote, state_.lastNoteLabel, sizeof(state_.lastNoteLabel));
}

void MidiEngine::updateNotesPerMinute()
{
    const uint32_t now = millis();
    uint16_t count = 0;
    for (int i = 0; i < 32; i++) {
        if (noteOnTimes[i] > 0 && now - noteOnTimes[i] < 60000) {
            count++;
        }
    }
    state_.notesPerMinute = count;
}

uint8_t MidiEngine::clampNote(int note)
{
    if (note < 0) return 0;
    if (note > 127) return 127;
    return static_cast<uint8_t>(note);
}
