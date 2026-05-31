#include "Board.h"

/**
 * @brief Implementation for the ESP32-S3-USB-OTG board.
 * Display init matches the verified monolithic sketch (commit 0dda545).
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
        // 1. LCD enable + init before USB host rails
        pinMode(5, OUTPUT);
        digitalWrite(5, LOW);

        Serial.println("[LCD] display->begin(80MHz)...");
        Serial.flush();
        if (!display->begin(80000000)) {
            Serial.println("[LCD] display->begin FAILED");
            Serial.flush();
            return false;
        }
        Serial.println("[LCD] display->begin OK");
        Serial.flush();

        pinMode(9, OUTPUT);
        digitalWrite(9, HIGH);

        // 2. USB host power
        pinMode(18 /* SEL */, OUTPUT); digitalWrite(18, HIGH);
        pinMode(12 /* VBUS */, OUTPUT); digitalWrite(12, HIGH);
        pinMode(17 /* LIMIT */, OUTPUT); digitalWrite(17, HIGH);
        pinMode(13 /* BOOST */, OUTPUT); digitalWrite(13, LOW);

        // 3. Buttons
        pinMode(0 /* OK/Boot */, INPUT_PULLUP);
        pinMode(10 /* UP */, INPUT_PULLUP);
        pinMode(11 /* DOWN */, INPUT_PULLUP);
        pinMode(14 /* MENU */, INPUT_PULLUP);

        analogReadResolution(12);
        return true;
    }

    Arduino_GFX* getDisplay() override {
        return display;
    }

    void setBacklight(uint8_t level) override {
        pinMode(9, OUTPUT);
        digitalWrite(9, level > 0 ? HIGH : LOW);
    }

    float getBatteryVoltage() override {
        const int raw = analogRead(2 /* BAT_VOLTS on esp32s3usbotg */);
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
