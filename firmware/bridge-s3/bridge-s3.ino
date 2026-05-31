// Generic USB MIDI to BLE MIDI bridge for ESP32-S3 boards with USB-OTG.
//
// Direction: USB MIDI keyboard/controller -> ESP32-S3 -> BLE MIDI peripheral.
// iPhone/iPad apps such as GarageBand connect from their Bluetooth MIDI menu.

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support. Classic ESP32 boards cannot host USB MIDI in firmware alone."
#endif

#include <Arduino_GFX_Library.h>
#include <esp_log.h>
#include "USBConnection.h"

static const char* TAG = "PianoBridge";
#include "BLEConnection.h"
#include "animation/BongoCat.h"
#include "BridgeUi.h"
#include "BridgeSettings.h"
#include "RTPMidiConfig.h"
#include "NetworkServices.h"
#include "MidiBridge.h"
#include "MidiCodec.h"

#ifndef BLE_DEVICE_NAME_TEXT
#define BLE_DEVICE_NAME_TEXT "Piano BLE Bridge"
#endif

#ifndef ENABLE_BLE_TO_USB
#define ENABLE_BLE_TO_USB 0
#endif

#ifndef RTP_MIDI_SESSION_NAME
#define RTP_MIDI_SESSION_NAME BLE_DEVICE_NAME_TEXT
#endif

static const char* kDefaultBleName = BLE_DEVICE_NAME_TEXT;
static const uint32_t SERIAL_BAUD = 115200;

// Official Espressif ESP32-S3-USB-OTG board controls.
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
    5, /* CS is 5 on v2.0 */
    LCD_SCLK_PIN,
    LCD_MOSI_PIN,
    GFX_NOT_DEFINED);

static Arduino_GFX* display = new Arduino_ST7789(
    displayBus,
    LCD_RESET_PIN,
    0,    /* Rotation */
    true, /* IPS */
    240,  /* Width */
    240,  /* Height */
    0, 0, 0, 0);

// Double-buffering canvas to eliminate all flicker.
static Arduino_Canvas* canvas = new Arduino_Canvas(240, 240, display);

BLEConnection bleMidi;

static bool displayReady = false;
static bool displayRefreshPending = true;
static BongoCatDisplay bongoCat;

static void updateDisplayDashboard(bool force = false);

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
        displayRefreshPending = true;
    }

    void onMidiDataReceived(const uint8_t* data, size_t length) override
    {
        uint8_t midiPacket[4] = {0};
        midiBridge.forward(data, length, midiPacket);
    }
};

UsbMidiInput usbMidi;

// Diagnostics task handles
static TaskHandle_t diagTaskHandle = nullptr;
static TaskHandle_t midiTaskHandle = nullptr;

static void monitorSystemResources(void* arg)
{
    (void)arg;
    for (;;) {
        const uint32_t freeHeap = esp_get_free_heap_size();
        const uint32_t minFreeHeap = esp_get_minimum_free_heap_size();
        ESP_LOGI("DIAG", "Heap: %lu B (min %lu)", freeHeap, minFreeHeap);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void midiProcessingTask(void* arg)
{
    (void)arg;
    for (;;) {
        usbMidi.task();
        bleMidi.task();
#if ENABLE_RTP_MIDI
        networkServices.task();
#endif
        vTaskDelay(1); 
    }
}

#if ENABLE_BLE_TO_USB
static void onBleMidiReceived(const uint8_t* data, size_t length)
{
    if (usbMidi.sendMidiMessage(data, length)) {
        // Stats tracked in midiBridge/counters if integrated, otherwise here
    }
    bridgeUi.notifyRawMidiEvent(data, length);
    displayRefreshPending = true;
}
#endif

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: ESP32-S3 with USB-OTG host");
}

static void initDisplay()
{
#if LCD_ENABLE_PIN >= 0
    pinMode(LCD_ENABLE_PIN, OUTPUT);
    digitalWrite(LCD_ENABLE_PIN, LOW);
#endif
#if LCD_BACKLIGHT_PIN >= 0
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
#endif

    displayReady = display->begin(80000000);
    if (!displayReady) {
        Serial.println("[LCD] Display init failed.");
        return;
    }
    if (!canvas->begin()) {
        Serial.println("[LCD] Canvas init failed.");
    }

    bridgeUi.begin(canvas, LCD_BACKLIGHT_PIN);
    bridgeUi.setUsbMidi(&usbMidi);
    bridgeUi.setBle(&bleMidi);
    bridgeUi.setMidiBridge(&midiBridge);
    bridgeUi.setBongoCat(&bongoCat);

    bongoCat.begin();
    updateDisplayDashboard(true);
}

static void initBoardUsbHostPower()
{
    Serial.println("[USB] Optimizing host power for S3-USB-OTG v2.0...");
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
    delay(500);
}

static void updateDisplayDashboard(bool force)
{
    if (!displayReady) return;
    const uint32_t nowMs = millis();
    bridgeUi.refresh(nowMs, force);
    canvas->flush();
}

static void printStatusIfChanged()
{
    static bool lastUsb = false, lastBle = false;
    static uint32_t lastStats = 0;
    const bool usb = usbMidi.isConnected(), ble = bleMidi.isConnected();
    if (usb != lastUsb || ble != lastBle) {
        Serial.printf("[STATUS] USB=%s BLE=%s\n", usb ? "connected" : "waiting", ble ? "connected" : "advertising");
        lastUsb = usb; lastBle = ble;
    }
    if (millis() - lastStats >= 10000) {
        lastStats = millis();
        const MidiBridge::Counters& s = midiBridge.counters();
        Serial.printf("[STATS] usb=%lu ble=%lu\n", s.usbPacketsSeen, s.blePacketsSent);
    }
    updateDisplayDashboard();
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    esp_log_level_set("*", ESP_LOG_INFO);
    bridgeSettings.begin(kDefaultBleName);
    bridgeUi.applySavedDisplayMode(bridgeSettings.displayModeIndex());
    printStartupBanner();
    initDisplay();
    initBoardUsbHostPower();
    if (usbMidi.begin()) Serial.println("[USB] Host ready.");
    bleMidi.begin(bridgeSettings.bleDeviceName());
    bridgeUi.setBle(&bleMidi);
    midiBridge.begin(&bleMidi, &bridgeSettings, &bridgeUi);
    xTaskCreatePinnedToCore(monitorSystemResources, "diag", 3072, nullptr, 1, &diagTaskHandle, 0);
    xTaskCreatePinnedToCore(midiProcessingTask, "midi", 4096, nullptr, 10, &midiTaskHandle, 1);
#if ENABLE_RTP_MIDI
    networkServices.begin(RTP_MIDI_SESSION_NAME, bridgeSettings.bleDeviceName());
    midiBridge.setNetwork(&networkServices);
#endif
#if ENABLE_BLE_TO_USB
    bleMidi.setMidiMessageCallback(onBleMidiReceived);
#endif
}

void loop()
{
    const uint32_t now = millis();
    bridgeUi.tick(now);
    printStatusIfChanged();
    vTaskDelay(pdMS_TO_TICKS(1));
}
