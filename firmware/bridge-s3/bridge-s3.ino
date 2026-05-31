#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include "Board.h"
#include "USBConnection.h"
#include "BLEConnection.h"
#include "ConnectivityManager.h"
#include "MidiBridge.h"
#include "MidiEngine.h"
#include "BridgeSettings.h"
#include "BridgeUi.h"
#include "animation/BongoCat.h"

// System Components
static Board* board = createBoard();
static Arduino_Canvas* canvas = nullptr;

static USBConnection usbMidi;
static BLEConnection bleMidi;
static BongoCatDisplay bongoCat;

void setup()
{
    Serial.begin(115200);
    
    // 1. Hardware Bootstrap
    if (!board->begin()) {
        Serial.println("[SYSTEM] Hardware initialization failed.");
        while(1) delay(100);
    }
    
    // 2. Settings and Canvas
    bridgeSettings.begin("Piano BLE Bridge");
    canvas = new Arduino_Canvas(240, 240, board->getDisplay());
    
    // 3. UI and Engine Setup
    if (canvas->begin()) {
        bridgeUi.begin(canvas, -1);
        bridgeUi.setBoard(board);
        bridgeUi.setMidiEngine(&midiEngine);
        bridgeUi.setBongoCat(&bongoCat);
    }
    
    // 4. MIDI Hub Coordination
    midiBridge.begin(&bridgeSettings, &bridgeUi);
    midiBridge.setMidiEngine(&midiEngine);
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    midiBridge.addTransport(&connectivityManager);

    // 5. Start Transports
    usbMidi.begin();
    bleMidi.begin(bridgeSettings.bleDeviceName());
    connectivityManager.begin();
    bongoCat.begin();

    // 6. Final UI Links
    bridgeUi.setUsbMidi(&usbMidi);
    bridgeUi.setBle(&bleMidi);
    bridgeUi.setMidiBridge(&midiBridge);

    Serial.println("[SYSTEM] Board-abstracted architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Periodic Maintenance
    board->task();
    usbMidi.task();
    bleMidi.task();
    connectivityManager.task();
    
    // UI Refresh
    bridgeUi.tick(now);
    bridgeUi.refresh(now);
    canvas->flush();
    
    vTaskDelay(pdMS_TO_TICKS(1));
}
