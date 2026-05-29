// Generic USB MIDI to BLE MIDI bridge for ESP32-S3 boards with USB-OTG.
//
// Direction: USB MIDI keyboard/controller -> ESP32-S3 -> BLE MIDI peripheral.
// iPhone/iPad apps such as GarageBand connect from their Bluetooth MIDI menu.

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "USB-MIDI-BLE-Bridge requires an ESP32-S3 board with native USB-OTG host support. Classic ESP32 boards cannot host USB MIDI in firmware alone."
#endif

#include <Arduino_GFX_Library.h>
#include "USBConnection.h"
#include "BLEConnection.h"

#ifndef BLE_DEVICE_NAME_TEXT
#define BLE_DEVICE_NAME_TEXT "Piano BLE Bridge"
#endif

static const char* BLE_DEVICE_NAME = BLE_DEVICE_NAME_TEXT;
static const uint32_t SERIAL_BAUD = 115200;

// Official Espressif ESP32-S3-USB-OTG board controls. These are harmless on
// generic S3 boards only if the pins are unconnected; override to -1 if needed.
#ifndef USB_HOST_SEL_PIN
#define USB_HOST_SEL_PIN 18
#endif

#ifndef USB_HOST_VBUS_EN_PIN
#define USB_HOST_VBUS_EN_PIN 12
#endif

#ifndef USB_HOST_LIMIT_EN_PIN
#define USB_HOST_LIMIT_EN_PIN 17
#endif

#ifndef USB_HOST_BOOST_EN_PIN
#define USB_HOST_BOOST_EN_PIN 13
#endif

#ifndef USB_HOST_POWER_FROM_BATTERY
#define USB_HOST_POWER_FROM_BATTERY 0
#endif

#ifndef LCD_ENABLE_PIN
#define LCD_ENABLE_PIN 5
#endif

#ifndef LCD_RESET_PIN
#define LCD_RESET_PIN 8
#endif

#ifndef LCD_DC_PIN
#define LCD_DC_PIN 4
#endif

#ifndef LCD_SCLK_PIN
#define LCD_SCLK_PIN 6
#endif

#ifndef LCD_MOSI_PIN
#define LCD_MOSI_PIN 7
#endif

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN 9
#endif

static Arduino_DataBus* displayBus = new Arduino_ESP32SPI(
    LCD_DC_PIN,
    GFX_NOT_DEFINED,
    LCD_SCLK_PIN,
    LCD_MOSI_PIN,
    GFX_NOT_DEFINED);

static Arduino_GFX* display = new Arduino_ST7789(
    displayBus,
    LCD_RESET_PIN,
    0,
    true,
    240,
    240,
    0,
    0,
    0,
    0);

BLEConnection bleMidi;

static uint32_t usbPacketsSeen = 0;
static uint32_t blePacketsSent = 0;
static uint32_t blePacketsSkipped = 0;
static bool displayReady = false;

static uint8_t midiLengthFromStatus(uint8_t status)
{
    if (status >= 0xF8) return 1; // Realtime messages.

    switch (status & 0xF0) {
        case 0xC0: // Program Change
        case 0xD0: // Channel Pressure
            return 2;
        case 0x80: // Note Off
        case 0x90: // Note On
        case 0xA0: // Poly Pressure
        case 0xB0: // Control Change
        case 0xE0: // Pitch Bend
            return 3;
        default:
            break;
    }

    switch (status) {
        case 0xF1: // MTC Quarter Frame
        case 0xF3: // Song Select
            return 2;
        case 0xF2: // Song Position Pointer
            return 3;
        case 0xF6: // Tune Request
            return 1;
        default:
            return 0;
    }
}

static uint8_t midiLengthFromUsbCin(uint8_t cin, uint8_t status)
{
    switch (cin & 0x0F) {
        case 0x2: return 2; // Two-byte system common.
        case 0x3: return 3; // Three-byte system common.
        case 0x4: return 3; // SysEx start/continue.
        case 0x5: return 1; // SysEx ends with one byte.
        case 0x6: return 2; // SysEx ends with two bytes.
        case 0x7: return 3; // SysEx ends with three bytes.
        case 0x8: return 3; // Note Off.
        case 0x9: return 3; // Note On.
        case 0xA: return 3; // Poly Pressure.
        case 0xB: return 3; // Control Change.
        case 0xC: return 2; // Program Change.
        case 0xD: return 2; // Channel Pressure.
        case 0xE: return 3; // Pitch Bend.
        case 0xF: return 1; // Single byte.
        default:
            return midiLengthFromStatus(status);
    }
}

static bool buildBleMidiPacket(const uint8_t* usbMidiPacket, size_t usbLength, uint8_t* blePacket, size_t* bleLength)
{
    if (usbLength < 4 || blePacket == nullptr || bleLength == nullptr) {
        return false;
    }

    const uint8_t cin = usbMidiPacket[0] & 0x0F;
    const uint8_t status = usbMidiPacket[1];
    const uint8_t midiLength = midiLengthFromUsbCin(cin, status);

    if (midiLength == 0 || midiLength > 3 || status == 0x00) {
        return false;
    }

    const uint16_t timestamp = millis() & 0x1FFF;
    blePacket[0] = 0x80 | ((timestamp >> 7) & 0x3F);
    blePacket[1] = 0x80 | (timestamp & 0x7F);

    for (uint8_t i = 0; i < midiLength; i++) {
        blePacket[2 + i] = usbMidiPacket[1 + i];
    }

    *bleLength = 2 + midiLength;
    return true;
}

static const char* statusName(uint8_t status)
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

class UsbMidiInput : public USBConnection {
public:
    void onDeviceConnected() override
    {
        Serial.println("[USB] MIDI device connected.");
        if (getLastError().length() > 0) {
            Serial.println("[USB] Last error: " + getLastError());
        }
    }

    void onDeviceDisconnected() override
    {
        Serial.println("[USB] MIDI device disconnected.");
    }

    void onMidiDataReceived(const uint8_t* data, size_t length) override
    {
        usbPacketsSeen++;

        uint8_t blePacket[5] = {0};
        size_t bleLength = 0;

        if (!buildBleMidiPacket(data, length, blePacket, &bleLength)) {
            if (length >= 2) {
                Serial.printf("[USB] Ignored packet CIN=%02X status=%02X\n", data[0] & 0x0F, data[1]);
            } else {
                Serial.printf("[USB] Ignored short packet length=%u\n", static_cast<unsigned>(length));
            }
            return;
        }

        Serial.printf("[USB] %-15s ch=%u data1=%3u data2=%3u BLE=%s\n",
                      statusName(data[1]),
                      ((data[1] & 0x0F) + 1),
                      data[2],
                      data[3],
                      bleMidi.isConnected() ? "connected" : "waiting");

        if (bleMidi.isConnected() && bleMidi.sendMidi(blePacket, bleLength)) {
            blePacketsSent++;
        } else {
            blePacketsSkipped++;
        }
    }
};

UsbMidiInput usbMidi;

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: ESP32-S3 with USB-OTG host");
    Serial.print("BLE name: ");
    Serial.println(BLE_DEVICE_NAME);
    Serial.println("Connect your iPhone from an app's Bluetooth MIDI device menu.");
    Serial.println();
}

static void printDisplayLine(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* text)
{
    if (!displayReady) {
        return;
    }

    display->setTextSize(size);
    display->setTextColor(color);
    display->setCursor(x, y);
    display->print(text);
}

static void initDisplay()
{
#if LCD_ENABLE_PIN >= 0
    pinMode(LCD_ENABLE_PIN, OUTPUT);
    digitalWrite(LCD_ENABLE_PIN, LOW);
#endif

#if LCD_BACKLIGHT_PIN >= 0
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
#endif

    displayReady = display->begin(40000000);
    if (!displayReady) {
        Serial.println("[LCD] Display init failed.");
        return;
    }

    display->fillScreen(RGB565_BLACK);
    display->fillRoundRect(8, 8, 224, 224, 12, RGB565_NAVY);
    display->drawRoundRect(8, 8, 224, 224, 12, RGB565_CYAN);
    display->fillRoundRect(18, 18, 204, 204, 8, RGB565_BLACK);

    printDisplayLine(34, 30, 2, RGB565_CYAN, "USB MIDI");
    printDisplayLine(28, 58, 3, RGB565_WHITE, "BRIDGE");
    printDisplayLine(28, 100, 2, RGB565_GREENYELLOW, "READY TO JAM");
    printDisplayLine(28, 138, 1, RGB565_LIGHTGRAY, "Bluetooth name:");
    printDisplayLine(28, 154, 2, RGB565_GOLD, BLE_DEVICE_NAME);
    printDisplayLine(28, 190, 1, RGB565_LIGHTGRAY, "USB: waiting");
    printDisplayLine(28, 206, 1, RGB565_GOLD, "Power USB_DEV port");
    printDisplayLine(28, 222, 1, RGB565_LIGHTGRAY, "BLE: advertising");
}

static void initBoardUsbHostPower()
{
#if USB_HOST_BOOST_EN_PIN >= 0
    pinMode(USB_HOST_BOOST_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_BOOST_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? HIGH : LOW);
#endif

#if USB_HOST_VBUS_EN_PIN >= 0
    pinMode(USB_HOST_VBUS_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_VBUS_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? LOW : HIGH);
#endif

#if USB_HOST_LIMIT_EN_PIN >= 0
    pinMode(USB_HOST_LIMIT_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_LIMIT_EN_PIN, HIGH);
#endif

#if USB_HOST_SEL_PIN >= 0
    pinMode(USB_HOST_SEL_PIN, OUTPUT);
    digitalWrite(USB_HOST_SEL_PIN, HIGH);
#endif

    delay(100);
}

static void updateDisplayStatus(bool usbConnected, bool bleConnected)
{
    if (!displayReady) {
        return;
    }

    display->fillRect(24, 188, 192, 42, RGB565_BLACK);
    printDisplayLine(28, 190, 1, usbConnected ? RGB565_LIME : RGB565_LIGHTGRAY,
                     usbConnected ? "USB: MIDI connected" : "USB: waiting");
    if (!usbConnected) {
        printDisplayLine(28, 206, 1, RGB565_GOLD, "Power USB_DEV port");
    }
    printDisplayLine(28, 222, 1, bleConnected ? RGB565_LIME : RGB565_LIGHTGRAY,
                     bleConnected ? "BLE: connected" : "BLE: advertising");
}

static void printStatusIfChanged()
{
    static bool lastUsbConnected = false;
    static bool lastBleConnected = false;
    static uint32_t lastSummaryMs = 0;

    const bool usbConnected = usbMidi.isConnected();
    const bool bleConnected = bleMidi.isConnected();

    if (usbConnected != lastUsbConnected || bleConnected != lastBleConnected) {
        Serial.printf("[STATUS] USB=%s BLE=%s\n",
                      usbConnected ? "connected" : "waiting",
                      bleConnected ? "connected" : "advertising");
        lastUsbConnected = usbConnected;
        lastBleConnected = bleConnected;
        updateDisplayStatus(usbConnected, bleConnected);
    }

    if (millis() - lastSummaryMs >= 10000) {
        lastSummaryMs = millis();
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu\n",
                      usbPacketsSeen,
                      blePacketsSent,
                      blePacketsSkipped);
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    printStartupBanner();
    initDisplay();
    initBoardUsbHostPower();

    if (usbMidi.begin()) {
        Serial.println("[USB] Host initialized. Waiting for a class-compliant USB MIDI device...");
    } else {
        Serial.println("[USB] Host init failed: " + usbMidi.getLastError());
    }

    bleMidi.begin(BLE_DEVICE_NAME);
    Serial.println("[BLE] MIDI server advertising.");
}

void loop()
{
    usbMidi.task();
    bleMidi.task();
    printStatusIfChanged();
    delayMicroseconds(50);
}
