#include "Board.h"

/**
 * @brief Implementation for the ESP32-S3-USB-OTG board.
 * Corrected to the verified SPI configuration from commit 0dda545.
 */
class S3UsbOtgBoard : public Board {
public:
    S3UsbOtgBoard() : 
        bus(new Arduino_ESP32SPI(
            4 /* DC */,
            5 /* CS */,
            6 /* SCLK */,
            7 /* MOSI */,
            GFX_NOT_DEFINED /* MISO */)),
        display(new Arduino_ST7789(
            bus,
            8 /* RST */,
            0 /* rotation */,
            true /* IPS */,
            240 /* width */,
            240 /* height */,
            0, 0, 0, 0))
    {}

    bool begin() override {
        // 1. USB Host Power Pins
        pinMode(18 /* SEL */, OUTPUT); digitalWrite(18, HIGH);
        pinMode(12 /* VBUS */, OUTPUT); digitalWrite(12, HIGH);
        pinMode(17 /* LIMIT */, OUTPUT); digitalWrite(17, HIGH);
        pinMode(13 /* BOOST */, OUTPUT); digitalWrite(13, LOW);

        // 2. LCD Initialization (SPI)
        if (!display->begin(40000000)) { 
            return false;
        }

        // 3. Backlight Setup (GPIO 9)
        pinMode(9 /* Backlight */, OUTPUT);
        setBacklight(255);

        // 4. Buttons (Pull-ups)
        pinMode(0 /* OK/Boot */, INPUT_PULLUP);
        pinMode(10 /* UP */, INPUT_PULLUP);
        pinMode(11 /* DOWN */, INPUT_PULLUP);
        pinMode(14 /* MENU */, INPUT_PULLUP);

        // 5. Battery Sensing
        analogReadResolution(12);

        return true;
    }

    Arduino_GFX* getDisplay() override {
        return display;
    }

    void setBacklight(uint8_t level) override {
        // Active High control on GPIO 9
        digitalWrite(9, level > 0 ? HIGH : LOW);
    }

    float getBatteryVoltage() override {
        const int raw = analogRead(1 /* BATT_SENSE */);
        return (raw / 4095.0f) * 3.3f * 2.0f;
    }

    bool isUsbPowered() override {
        return getBatteryVoltage() > 4.4f;
    }

    int getButtonPin(const char* name) override {
        if (strcmp(name, "OK") == 0) return 0;
        if (strcmp(name, "UP") == 0) return 10;
        if (strcmp(name, "DOWN") == 0) return 11;
        if (strcmp(name, "MENU") == 0) return 14;
        return -1;
    }

private:
    Arduino_DataBus* bus;
    Arduino_GFX* display;
};

Board* createBoard() {
    return new S3UsbOtgBoard();
}
