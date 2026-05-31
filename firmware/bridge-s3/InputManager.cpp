#include "InputManager.h"

InputManager inputManager;

InputManager::InputManager() {}

void InputManager::onEvent(const char* buttonName, InputCallback cb)
{
    buttons_[buttonName].callback = cb;
}

void InputManager::mapButton(const char* name, int pin)
{
    if (pin < 0) return;
    buttons_[name].pin = pin;
    pinMode(pin, INPUT_PULLUP);
}

void InputManager::task(uint32_t nowMs)
{
    for (auto& pair : buttons_) {
        ButtonState& b = pair.second;
        if (b.pin < 0) continue;

        bool rawDown = (digitalRead(b.pin) == LOW);
        
        // 1. Edge Detection (Press)
        if (rawDown && !b.isDown) {
            b.isDown = true;
            b.downMs = nowMs;
            b.actionFired = false;
        }

        // 2. Edge Detection (Release)
        if (!rawDown && b.isDown) {
            if (!b.actionFired && (nowMs - b.downMs < kLongHoldMs)) {
                if (b.callback) b.callback(Event::kTap);
            }
            b.isDown = false;
            b.actionFired = false;
        }

        // 3. Long Hold Detection
        if (rawDown && b.isDown && !b.actionFired) {
            if (nowMs - b.downMs >= kLongHoldMs) {
                if (b.callback) b.callback(Event::kLongHold);
                b.actionFired = true;
            }
        }
    }
}
