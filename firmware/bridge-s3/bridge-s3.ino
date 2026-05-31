#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include "Board.h"
#include "InputManager.h"
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
    
    // 3. Input Mapping (Board -> InputManager -> UI)
    inputManager.mapButton("OK", board->getButtonPin("OK"));
    inputManager.mapButton("UP", board->getButtonPin("UP"));
    inputManager.mapButton("DOWN", board->getButtonPin("DOWN"));
    inputManager.mapButton("MENU", board->getButtonPin("MENU"));

    inputManager.onEvent("OK", [](InputManager::Event e){ 
        if (e == InputManager::Event::kTap) bridgeUi.onOkTap();
        else if (e == InputManager::Event::kLongHold) bridgeUi.onOkHold();
    });
    inputManager.onEvent("UP", [](InputManager::Event e){ if (e == InputManager::Event::kTap) bridgeUi.onUpTap(); });
    inputManager.onEvent("DOWN", [](InputManager::Event e){ if (e == InputManager::Event::kTap) bridgeUi.onDownTap(); });
    inputManager.onEvent("MENU", [](InputManager::Event e){
        if (e == InputManager::Event::kTap) bridgeUi.onMenuTap();
        else if (e == InputManager::Event::kLongHold) bridgeUi.onMenuHold();
    });

    // 4. UI and Engine Setup
    if (canvas->begin()) {
        bridgeUi.begin(canvas);
        bridgeUi.setBoard(board);
        bridgeUi.setMidiEngine(&midiEngine);
        bridgeUi.setBongoCat(&bongoCat);
    }
    
    // 5. MIDI Hub Coordination
    midiBridge.begin(&bridgeSettings, &bridgeUi);
    midiBridge.setMidiEngine(&midiEngine);
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    midiBridge.addTransport(&connectivityManager);

    // 6. Start Transports
    usbMidi.begin();
    bleMidi.begin(bridgeSettings.bleDeviceName());
    connectivityManager.begin();
    bongoCat.begin();

    // 7. Final UI Links
    bridgeUi.setUsbMidi(&usbMidi);
    bridgeUi.setBle(&bleMidi);
    bridgeUi.setMidiBridge(&midiBridge);

    Serial.println("[SYSTEM] Minimal Rack architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Periodic Maintenance
    board->task();
    inputManager.task(now);
    usbMidi.task();
    bleMidi.task();
    connectivityManager.task();
    
    // UI Refresh
    bridgeUi.tick(now);
    bridgeUi.refresh(now);
    canvas->flush();
    
    vTaskDelay(pdMS_TO_TICKS(1));
}
