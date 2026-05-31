#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include <functional>
#include <map>

/**
 * @brief Logical Input Manager.
 * 
 * Deep Module Principle: Translates raw physical button states into logical events.
 * Leverage: The UI only reacts to "Events" (e.g., onMenuHold) rather than polling pins.
 */
class InputManager {
public:
    enum class Event {
        kTap,
        kLongHold,
        kDoubleTap // Optional for future
    };

    using InputCallback = std::function<void(Event)>;

    InputManager();

    /** @brief Registers a callback for a named logical button (e.g., "OK", "MENU"). */
    void onEvent(const char* buttonName, InputCallback cb);

    /** @brief Maps a logical button to a physical pin. */
    void mapButton(const char* name, int pin);

    /** @brief Must be called periodically to debounce and detect events. */
    void task(uint32_t nowMs);

private:
    struct ButtonState {
        int pin = -1;
        bool lastRawState = HIGH;
        bool isDown = false;
        uint32_t downMs = 0;
        bool actionFired = false;
        InputCallback callback = nullptr;
    };

    std::map<String, ButtonState> buttons_;
    static constexpr uint32_t kShortPressMaxMs = 400;
    static constexpr uint32_t kLongHoldMs = 1000;
};

extern InputManager inputManager;

#endif
