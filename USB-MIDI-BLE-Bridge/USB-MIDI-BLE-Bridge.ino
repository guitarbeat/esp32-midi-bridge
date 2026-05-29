// Generic USB MIDI to BLE MIDI bridge for ESP32-S3 boards with USB-OTG.
//
// Direction: USB MIDI keyboard/controller -> ESP32-S3 -> BLE MIDI peripheral.
// iPhone/iPad apps such as GarageBand connect from their Bluetooth MIDI menu.

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "USB-MIDI-BLE-Bridge requires an ESP32-S3 board with native USB-OTG host support. Classic ESP32 boards cannot host USB MIDI in firmware alone."
#endif

#include <Arduino_GFX_Library.h>
#include <esp_log.h>
#include "USBConnection.h"

static const char* TAG = "PianoBridge";
#include "BLEConnection.h"
#include "bongo_cat/BongoCat.h"
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
    5, /* CS is 5 on v2.0 */
    LCD_SCLK_PIN,
    LCD_MOSI_PIN,
    GFX_NOT_DEFINED /* MISO */);

static Arduino_GFX* display = new Arduino_ST7789(
    displayBus,
    LCD_RESET_PIN,
    0,    /* Rotation */
    true, /* IPS */
    240,  /* Width */
    240,  /* Height */
    0, 0, 0, 0);

// Double-buffering canvas to eliminate all flicker.
// 240x240x16bpp = 115,200 bytes, safe for S3 internal RAM.
static Arduino_Canvas* canvas = new Arduino_Canvas(240, 240, display);

BLEConnection bleMidi;

static uint32_t midiEventsSeen = 0;
static uint32_t noteEventsSeen = 0;
static uint32_t blePacketsReceived = 0;
static uint32_t usbPacketsSent = 0;
static uint32_t usbPacketsSkipped = 0;
static bool displayReady = false;
static uint32_t lastMidiMs = 0;
static char lastMidiText[36] = "none";
static bool displayRefreshPending = true;
static bool displayStaticDrawn = false;
static BongoCatDisplay bongoCat;

static void updateDisplayDashboard(bool force = false);
static void markDisplayMidiEvent(const uint8_t* data);
static void markDisplayRawMidiEvent(const uint8_t* data, size_t length);

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
        const MidiBridge::Result result = midiBridge.forward(data, length, midiPacket);
        if (result == MidiBridge::Result::kForwarded) {
            markDisplayMidiEvent(midiPacket);
        }
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
        
        // Check stack high water marks
        if (diagTaskHandle) ESP_LOGI("DIAG", "Stack Diag: %u", uxTaskGetStackHighWaterMark(diagTaskHandle));
        if (midiTaskHandle) ESP_LOGI("DIAG", "Stack MIDI: %u", uxTaskGetStackHighWaterMark(midiTaskHandle));
        if (usbMidi.getTaskHandle()) ESP_LOGI("DIAG", "Stack USB: %u", uxTaskGetStackHighWaterMark(usbMidi.getTaskHandle()));
        
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
        // Small yield to prevent starving lower priority tasks on Core 1 if loop is tight
        vTaskDelay(1); 
    }
}

#if ENABLE_BLE_TO_USB
static void onBleMidiReceived(const uint8_t* data, size_t length)
{
    blePacketsReceived++;
    if (usbMidi.sendMidiMessage(data, length)) {
        usbPacketsSent++;
    } else {
        usbPacketsSkipped++;
    }
    markDisplayRawMidiEvent(data, length);
    displayRefreshPending = true;
}
#endif

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: ESP32-S3 with USB-OTG host");
    Serial.print("BLE name: ");
    Serial.println(bridgeSettings.bleDeviceName());
    Serial.println("Connect your iPhone from an app's Bluetooth MIDI device menu.");
    Serial.println();
}

static void printDisplayLine(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* text)
{
    if (!displayReady) {
        return;
    }

    canvas->setTextSize(size);
    canvas->setTextColor(color);
    canvas->setCursor(x, y);
    canvas->print(text);
}

static void initDisplay()
{
#if LCD_ENABLE_PIN >= 0
    pinMode(LCD_ENABLE_PIN, OUTPUT);
    digitalWrite(LCD_ENABLE_PIN, LOW); // Active Low
#endif

#if LCD_BACKLIGHT_PIN >= 0
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
#endif

    // ESP32-S3-USB-OTG ST7789 supports up to 80MHz SPI.
    displayReady = display->begin(80000000);
    if (!displayReady) {
        Serial.println("[LCD] Display init failed.");
        return;
    }

    if (!canvas->begin()) {
        Serial.println("[LCD] Canvas init failed.");
    }

    bridgeUi.begin(canvas, LCD_BACKLIGHT_PIN);
    bongoCat.begin();
    displayStaticDrawn = false;
    updateDisplayDashboard(true);
}

static void initBoardUsbHostPower()
{
    Serial.println("[USB] Optimizing host power for S3-USB-OTG v2.0...");
#if USB_HOST_BOOST_EN_PIN >= 0
    pinMode(USB_HOST_BOOST_EN_PIN, OUTPUT);
    // Enable battery boost only if explicitly requested, otherwise use USB_DEV power
    digitalWrite(USB_HOST_BOOST_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? HIGH : LOW);
#endif

#if USB_HOST_VBUS_EN_PIN >= 0
    pinMode(USB_HOST_VBUS_EN_PIN, OUTPUT);
    // High = VBUS from USB_DEV (standard configuration)
    digitalWrite(USB_HOST_VBUS_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? LOW : HIGH);
#endif

#if USB_HOST_LIMIT_EN_PIN >= 0
    pinMode(USB_HOST_LIMIT_EN_PIN, OUTPUT);
    // Enable current limit switch
    digitalWrite(USB_HOST_LIMIT_EN_PIN, HIGH);
#endif

#if USB_HOST_SEL_PIN >= 0
    pinMode(USB_HOST_SEL_PIN, OUTPUT);
    // Select Type-A Host port
    digitalWrite(USB_HOST_SEL_PIN, HIGH);
#endif

    // Stabilization delay for VBUS
    delay(500);
    Serial.println("[USB] Host power rail stable.");
}

static void updateDisplayStatus(bool usbConnected, bool bleConnected)
{
    (void)usbConnected;
    (void)bleConnected;
    displayRefreshPending = true;
}

static void markDisplayRawMidiEvent(const uint8_t* data, size_t length)
{
    uint8_t usbPacket[4] = {0, 0, 0, 0};
    if (length >= 1) {
        usbPacket[1] = data[0];
    }
    if (length >= 2) {
        usbPacket[2] = data[1];
    }
    if (length >= 3) {
        usbPacket[3] = data[2];
    }
    markDisplayMidiEvent(usbPacket);
}

static void markDisplayMidiEvent(const uint8_t* data)
{
    midiEventsSeen++;

    const uint8_t status = data[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;
    const uint8_t data1 = data[2];
    const uint8_t data2 = data[3];
    char noteBuffer[6] = {0};

    if ((messageType == 0x90 && data2 > 0) || messageType == 0x80 || (messageType == 0x90 && data2 == 0)) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        noteEventsSeen++;

        bridgeUi.onNoteEvent(noteOn, data1, data2);

        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s %s ch%u v%u",
                 noteOn ? "On" : "Off",
                 MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer)),
                 channel,
                 data2);
    } else if (messageType == 0xB0) {
        bridgeUi.onControlChange(data1, data2);
        snprintf(lastMidiText, sizeof(lastMidiText), "CC %u=%u ch%u", data1, data2, channel);
    } else if (messageType == 0xE0) {
        const uint16_t bend = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
        snprintf(lastMidiText, sizeof(lastMidiText), "Bend %u ch%u", bend, channel);
    } else {
        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s ch%u %u %u",
                 MidiCodec::statusName(status),
                 channel,
                 data1,
                 data2);
    }

    lastMidiMs = millis();
    bridgeUi.touchActivity(lastMidiMs);
    displayRefreshPending = true;
}

static void printMetricLine(int16_t y, const char* label, const char* value, uint16_t valueColor)
{
    printDisplayLine(18, y, 1, RGB565_LIGHTGRAY, label);
    printDisplayLine(82, y, 1, valueColor, value);
}

static void tickBongoCat(uint32_t nowMs)
{
    // Bongo cat update/draw is now merged into updateDisplayDashboard for synced double-buffering.
    (void)nowMs;
}

static void drawStatusPill(int16_t x, int16_t y, const char* label, const char* value, bool ok)
{
    const uint16_t color = ok ? RGB565_LIME : RGB565_GOLD;
    const uint16_t bg = ok ? RGB565(0, 40, 20) : RGB565(40, 30, 0);

    canvas->fillRoundRect(x, y, 96, 42, 6, bg);
    canvas->drawRoundRect(x, y, 96, 42, 6, color);
    
    canvas->setTextSize(1);
    canvas->setTextColor(RGB565_LIGHTGRAY);
    canvas->setCursor(x + 8, y + 6);
    canvas->print(label);
    
    canvas->setTextSize(2);
    canvas->setTextColor(color);
    canvas->setCursor(x + 8, y + 19);
    canvas->print(value);
}

static void updateDisplayDashboard(bool force)
{
    if (!displayReady) {
        return;
    }

    static uint32_t lastDrawMs = 0;
    const uint32_t nowMs = millis();
    
    // Smooth 10Hz UI refresh
    if (!force && nowMs - lastDrawMs < 100) {
        return;
    }

    lastDrawMs = nowMs;
    displayRefreshPending = false;

    const bool usbConnected = usbMidi.isConnected();
    const bool bleConnected = bleMidi.isConnected();
    
    // Start drawing to canvas
    canvas->fillScreen(RGB565_BLACK);
    
    // Frame Decoration
    canvas->drawRoundRect(4, 4, 232, 232, 12, RGB565_CYAN);
    canvas->drawRoundRect(5, 5, 230, 230, 11, RGB565(0, 64, 128));

    // Header Area
    canvas->setTextSize(2);
    canvas->setTextColor(RGB565_CYAN);
    canvas->setCursor(20, 18);
    canvas->print("PIANO BLE");
    
    canvas->setTextSize(1);
    canvas->setTextColor(RGB565_GOLD);
    canvas->setCursor(20, 40);
    canvas->print(bridgeSettings.bleDeviceName());

    if (bridgeUi.shouldDrawStatusPanel()) {
        const int16_t statusY = BongoCatDisplay::kStatusTop;
        
        // Modern Status Pills
        drawStatusPill(18, statusY + 4, "USB HOST", usbConnected ? "OK" : "WAIT", usbConnected);
        drawStatusPill(126, statusY + 4, "BLUETOOTH", bleConnected ? "OK" : "READY", bleConnected);
        
        if (usbConnected && usbMidi.getDeviceName().length() > 0) {
            canvas->setTextSize(1);
            canvas->setTextColor(RGB565_WHITE);
            canvas->setCursor(22, statusY + 34);
            canvas->printf("Device: %.24s", usbMidi.getDeviceName().c_str());
        }

        const MidiBridge::Counters& midiStats = midiBridge.counters();
        
        if (bridgeUi.shouldDrawFullMetrics()) {
            char val1[32], val2[32];
            snprintf(val1, sizeof(val1), "Notes %lu", noteEventsSeen);
            snprintf(val2, sizeof(val2), "Held %u", bridgeUi.heldNoteCount());
            
            canvas->setTextColor(RGB565_LIGHTGRAY);
            canvas->setCursor(22, statusY + 48);
            canvas->print(val1);
            canvas->setCursor(110, statusY + 48);
            canvas->print(val2);

            snprintf(val1, sizeof(val1), "%u /min", bridgeUi.notesPerMinute());
            snprintf(val2, sizeof(val2), "BLE %lu", midiStats.blePacketsSent);
            canvas->setCursor(22, statusY + 60);
            canvas->print(val1);
            canvas->setCursor(110, statusY + 60);
            canvas->print(val2);

#if ENABLE_BLE_TO_USB
            canvas->setCursor(22, statusY + 72);
            canvas->printf("Out: %lu", usbPacketsSent);
#else
            if (bridgeSettings.transposeSemitones() != 0) {
                canvas->setTextColor(RGB565_GOLD);
                canvas->setCursor(22, statusY + 72);
                canvas->printf("Transpose %+d", bridgeSettings.transposeSemitones());
            } else {
                canvas->setTextColor(RGB565_DARKGRAY);
                canvas->setCursor(22, statusY + 72);
                canvas->print(lastMidiText);
            }
#endif
        }

        // Footer Hint
        canvas->setTextColor(usbConnected && bleConnected ? RGB565_LIME : RGB565_GOLD);
        canvas->setCursor(22, statusY + 86);
        if (!usbConnected) canvas->print("Connect MIDI to Host port");
        else if (!bleConnected) canvas->print("Pair iPad to BLE");
        else canvas->print("Ready to play");
    }

    // Animation layer
    const bool midiActive = lastMidiMs > 0 && nowMs - lastMidiMs < 600;
    bongoCat.update(nowMs, midiActive, noteEventsSeen);
    bongoCat.draw(canvas);
    
    // Overlays (Velocity, Keyboard, Toasts)
    bridgeUi.drawOverlays(nowMs);
    
    // Commit everything to display
    canvas->flush();
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
        const MidiBridge::Counters& midiStats = midiBridge.counters();
#if ENABLE_BLE_TO_USB
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu ble_in=%lu usb_out=%lu/%lu can_send=%s\n",
                      midiStats.usbPacketsSeen,
                      midiStats.blePacketsSent,
                      midiStats.blePacketsSkipped,
                      blePacketsReceived,
                      usbPacketsSent,
                      usbPacketsSkipped,
                      usbMidi.canSend() ? "yes" : "no");
#else
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu\n",
                      midiStats.usbPacketsSeen,
                      midiStats.blePacketsSent,
                      midiStats.blePacketsSkipped);
#endif
        displayRefreshPending = true;
    }

    updateDisplayDashboard();
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    // Set log level for the application tag and diagnostics
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("DIAG", ESP_LOG_INFO);

    bridgeSettings.begin(kDefaultBleName);
    bridgeUi.applySavedDisplayMode(bridgeSettings.displayModeIndex());

    printStartupBanner();
    bridgeSettings.printSummary();
    initDisplay();
    initBoardUsbHostPower();

    if (usbMidi.begin()) {
        Serial.println("[USB] Host initialized. Waiting for a class-compliant USB MIDI device...");
    } else {
        Serial.println("[USB] Host init failed: " + usbMidi.getLastError());
    }

    bleMidi.begin(bridgeSettings.bleDeviceName());
    bridgeUi.setBle(&bleMidi);
    midiBridge.begin(&bleMidi, &bridgeSettings, &bridgeUi);

    // Start diagnostics task on Core 0 (low priority)
    xTaskCreatePinnedToCore(monitorSystemResources, "diag", 3072, nullptr, 1, &diagTaskHandle, 0);

    // Start high-priority MIDI processing task on Core 1
    // Higher priority than the main Arduino loop (which is priority 1)
    xTaskCreatePinnedToCore(midiProcessingTask, "midi_bridge", 4096, nullptr, 10, &midiTaskHandle, 1);

#if ENABLE_RTP_MIDI
    networkServices.begin(RTP_MIDI_SESSION_NAME, bridgeSettings.bleDeviceName());
    midiBridge.setNetwork(&networkServices);
#endif
#if ENABLE_BLE_TO_USB
    bleMidi.setMidiMessageCallback(onBleMidiReceived);
#endif
    Serial.println("[BLE] MIDI server advertising.");
}

void loop()
{
    // MIDI, BLE, and Network tasks are now handled in midi_bridge task.
    // The main loop focuses on UI and animations.
    
    const uint32_t nowMs = millis();
    bridgeUi.tick(nowMs);
    tickBongoCat(nowMs);
    printStatusIfChanged();
    
    // Yield to let other tasks run on Core 1 if needed
    vTaskDelay(pdMS_TO_TICKS(1));
}
