#include "BridgeController.h"

BridgeController bridgeController;

void BridgeController::begin()
{
    // 1. Initialize Settings (Memory)
    settings_.begin("Piano-BLE-Bridge");

    // 2. Synchronize Engine (Live) with Settings
    engine_.setTranspose(settings_.transposeSemitones());
    engine_.setChannelFilter(settings_.midiChannelFilter());

    Serial.println("[SYSTEM] Brain initialized and synchronized.");
}

void BridgeController::stepTranspose(int8_t delta)
{
    // Update Memory
    settings_.stepTranspose(delta);
    
    // Update Live Engine
    engine_.setTranspose(settings_.transposeSemitones());
}

void BridgeController::cycleMidiChannel()
{
    // Update Memory
    settings_.cycleMidiChannelFilter();
    
    // Update Live Engine
    engine_.setChannelFilter(settings_.midiChannelFilter());
}

void BridgeController::cycleBacklightDim()
{
    settings_.cycleBacklightDim();
}

void BridgeController::togglePause()
{
    bridgePaused_ = !bridgePaused_;
}

void BridgeController::sendPanic()
{
    // Trigger "All Notes Off" on the engine state and transport logic
    // (Implementation detail: UI and Bridge will react to this)
    Serial.println("[SYSTEM] Panic triggered.");
}

const char* BridgeController::transposeString() const
{
    int8_t val = settings_.transposeSemitones();
    snprintf((char*)transposeBuf_, sizeof(transposeBuf_), "%s%d", val > 0 ? "+" : "", val);
    return transposeBuf_;
}

const char* BridgeController::channelString() const
{
    uint8_t val = settings_.midiChannelFilter();
    if (val == 0) return "ALL";
    snprintf((char*)channelBuf_, sizeof(channelBuf_), "CH%u", val);
    return channelBuf_;
}

void BridgeController::tick(uint32_t nowMs)
{
    engine_.tick(nowMs);
}
