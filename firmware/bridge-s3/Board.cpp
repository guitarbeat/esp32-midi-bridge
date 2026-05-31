#include "Board.h"

/**
 * @brief Implementation for the ESP32-S3-USB-OTG board.
 * Display wiring matches Espressif's esp32s3usbotg variant (ST7789 on SPI, BL=GPIO9, CS/EN=GPIO5).
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

        // 2. LCD power / reset (official board: GPIO5 active-low enable, GPIO9 backlight, GPIO8 reset)
        pinMode(5, OUTPUT);
        digitalWrite(5, LOW);  // display enable (active low, shared with SPI CS)
        pinMode(9, OUTPUT);
        digitalWrite(9, LOW);  // keep backlight off until init completes

        pinMode(8, OUTPUT);
        digitalWrite(8, LOW);
        delay(20);
        digitalWrite(8, HIGH);
        delay(120);

        // 3. LCD Initialization — 80 MHz verified on ESP32-S3-USB-OTG ST7789
        if (!display->begin(80000000)) {
            return false;
        }

        // 4. Buttons (GPIO14 is MENU on this board — do not repurpose as display power)
        pinMode(0 /* OK/Boot */, INPUT_PULLUP);
        pinMode(10 /* UP */, INPUT_PULLUP);
        pinMode(11 /* DOWN */, INPUT_PULLUP);
        pinMode(14 /* MENU */, INPUT_PULLUP);

        // 5. Battery Sensing (BAT_VOLTS = ADC1 ch1 = GPIO2 on esp32s3usbotg)
        analogReadResolution(12);

        return true;
    }

    Arduino_GFX* getDisplay() override {
        return display;
    }

    void setBacklight(uint8_t level) override {
        if (!backlightPwmAttached_) {
            ledcAttach(9, 5000, 8);
            backlightPwmAttached_ = true;
        }
        ledcWrite(9, level);
    }

    float getBatteryVoltage() override {
        const int raw = analogRead(2 /* BAT_VOLTS */);
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
    bool backlightPwmAttached_ = false;
};

Board* createBoard() {
    return new S3UsbOtgBoard();
}
