#ifndef BOARD_H
#define BOARD_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

/**
 * @brief The Board Abstraction (Hardware Seam).
 * 
 * Deep Module Principle: Encapsulates all physical pin mappings and low-level drivers.
 * High Leverage: The rest of the system uses logical calls (e.g., setBacklight) 
 * without knowing which pins are used or how the hardware is wired.
 */
class Board {
public:
    virtual ~Board() = default;

    /** @brief Initializes all board-specific hardware (Bus, Pins, Serial). */
    virtual bool begin() = 0;

    /** @brief Returns the display driver instance. */
    virtual Arduino_GFX* getDisplay() = 0;

    /** @brief Sets the display backlight level (0-255). */
    virtual void setBacklight(uint8_t level) = 0;

    /** @brief Returns the current battery voltage in Volts. */
    virtual float getBatteryVoltage() = 0;

    /** @brief Returns true if the board is currently powered via USB. */
    virtual bool isUsbPowered() = 0;

    /** @brief Returns the hardware pin for a logical button name. */
    virtual int getButtonPin(const char* name) = 0;
    
    /** @brief Optional periodic hardware maintenance (e.g. smoothing ADC reads). */
    virtual void task() {}

    /** @brief DIN MIDI UART RX pin (must not be GPIO 43/44 on ESP32-S3). */
    virtual int uartMidiRxPin() const { return 48; }

    /** @brief DIN MIDI UART TX pin (must not be GPIO 43/44 on ESP32-S3). */
    virtual int uartMidiTxPin() const { return 47; }
};

/** @brief Factory function to create the specific board instance. */
Board* createBoard();

#endif
