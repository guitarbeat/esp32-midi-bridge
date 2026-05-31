#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include <Arduino_GFX_Library.h>
#include "USBConnection.h"
#include "BLEConnection.h"
#include "ConnectivityManager.h"
#include "MidiBridge.h"
#include "MidiEngine.h"
#include "BridgeSettings.h"
#include "BridgeUi.h"
#include "animation/BongoCat.h"

// Hardware Configuration
#define LCD_BACKLIGHT_PIN 14
#define LCD_ENABLE_PIN -1

static Arduino_DataBus* bus = new Arduino_ESP32LCD8080(
    45 /* DC */, 0 /* CS */, 47 /* WR */, 21 /* RD */,
    39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */,
    45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */);

static Arduino_GFX* display = new Arduino_ST7789(
    bus, -1 /* RST */, 0 /* rotation */, true /* IPS */,
    240 /* width */, 240 /* height */,
    0 /* col_offset1 */, 0 /* row_offset1 */,
    0 /* col_offset2 */, 0 /* row_offset2 */);

static Arduino_Canvas* canvas = new Arduino_Canvas(240, 240, display);

// Modules
static USBConnection usbMidi;
static BLEConnection bleMidi;
static BongoCatDisplay bongoCat;

void setup()
{
    Serial.begin(115200);
    bridgeSettings.begin("Piano BLE Bridge");

    // Initialize UI and Engine
    if (canvas->begin()) {
        bridgeUi.begin(canvas, LCD_BACKLIGHT_PIN);
        bridgeUi.setMidiEngine(&midiEngine);
        bridgeUi.setBongoCat(&bongoCat);
    }
    
    // Initialize MIDI Bridge with Transports
    midiBridge.begin(&bridgeSettings, &bridgeUi);
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    midiBridge.addTransport(&connectivityManager);

    // Start Hardware
    usbMidi.begin();
    bleMidi.begin(bridgeSettings.bleDeviceName());
    connectivityManager.begin();
    bongoCat.begin();

    // Link UI pointers
    bridgeUi.setUsbMidi(&usbMidi);
    bridgeUi.setBle(&bleMidi);
    bridgeUi.setMidiBridge(&midiBridge);

    Serial.println("[SYSTEM] Deep Module architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Process All Modules
    usbMidi.task();
    bleMidi.task();
    connectivityManager.task();
    
    // UI Refresh
    bridgeUi.tick(now);
    bridgeUi.refresh(now);
    canvas->flush();
    
    vTaskDelay(1);
}
