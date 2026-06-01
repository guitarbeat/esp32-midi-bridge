#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include "Board.h"
#include "InputManager.h"
#include "BridgeLog.h"
#include "WifiDebugLog.h"
#include "USBConnection.h"
#include "BLEConnection.h"
#include "UartConnection.h"
#include "ConnectivityManager.h"
#include "BridgeSystem.h"
#include "MidiBridge.h"
#include "BridgeUi.h"

// System Components — canvas allocated at static init (115 KB) while heap is clean.
static Board* board = createBoard();
static Arduino_Canvas* canvas = new Arduino_Canvas(240, 240, board->getDisplay());

static USBConnection usbMidi;
static BLEConnection bleMidi;
static UartConnection uartMidi(Serial2, 48 /* RX */, 47 /* TX */);

static BridgeUiRouteStats toUiStats(const MidiBridge::RouteStats& stats)
{
    BridgeUiRouteStats uiStats;
    uiStats.received = stats.received;
    uiStats.sent = stats.sent;
    uiStats.skipped = stats.skipped;
    uiStats.failed = stats.failed;
    return uiStats;
}

void setup()
{
    Serial.begin(115200);
    
    // Wait for native USB CDC Serial connection to establish so we don't miss boot logs
    uint32_t startWait = millis();
    while (!Serial && (millis() - startWait < 5000)) {
        delay(10);
    }
    Serial.println("\n[SYSTEM] Native USB CDC Serial connected. Booting...");
    wifiDebugLogBegin();
    
    // 1. Hardware Bootstrap
    if (!board->begin()) {
        Serial.println("[SYSTEM] Hardware initialization failed (LCD SPI).");
    } else {
        Serial.println("[SYSTEM] LCD panel initialized.");
    }
    
    // 2. System Controller (Brain)
    bridgeSystem.begin();
    
    // 3. UI and Graphics framebuffer (panel already initialized in board->begin)
    Serial.println("[SYSTEM] Initializing display canvas...");
    Serial.flush();
    if (canvas != nullptr && canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.printf("[SYSTEM] Canvas framebuffer OK (%u bytes free heap)\n", ESP.getFreeHeap());
        Serial.flush();
        bridgeUi.begin(canvas);
        bridgeUi.setDisplayMode(static_cast<BridgeUi::DisplayMode>(bridgeSystem.settings().displayModeIndex()));
        bridgeUi.setBoard(board);
        board->setBacklight(255);
        bridgeUi.refresh(millis(), true);
        canvas->flush();
        Serial.println("[SYSTEM] Display canvas initialized.");
        Serial.flush();
    } else {
        Serial.printf("[SYSTEM] ERROR: Canvas init failed (%u bytes free heap)\n", ESP.getFreeHeap());
        Serial.flush();
    }
    
    // 4. Input Mapping
    inputManager.mapButton("OK", board->getButtonPin("OK"));
    inputManager.mapButton("UP", board->getButtonPin("UP"));
    inputManager.mapButton("DOWN", board->getButtonPin("DOWN"));
    inputManager.mapButton("MENU", board->getButtonPin("MENU"));

    inputManager.onEvent("OK", [](InputManager::Event e){ 
        if (e == InputManager::Event::kTap) bridgeUi.cycleDisplayMode();
        else if (e == InputManager::Event::kLongHold) bridgeSystem.sendPanic();
    });
    inputManager.onEvent("UP", [](InputManager::Event e){ 
        if (e == InputManager::Event::kTap) bridgeSystem.stepTranspose(1); 
        else if (e == InputManager::Event::kLongHold) bridgeSystem.cycleMidiChannel();
    });
    inputManager.onEvent("DOWN", [](InputManager::Event e){ 
        if (e == InputManager::Event::kTap) bridgeSystem.stepTranspose(-1); 
        else if (e == InputManager::Event::kLongHold) connectivityManager.startProvisioning();
    });
    inputManager.onEvent("MENU", [](InputManager::Event e){
        if (e == InputManager::Event::kTap) bridgeSystem.cycleBacklightDim();
    });

    // 5. MIDI Hub Coordination
    midiBridge.begin(&bridgeUi, []() { return bridgeSystem.isPaused(); });
    midiBridge.setMidiEngine(&bridgeSystem.engine());
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    if (bridgeSystem.settings().uartEnabled()) {
        midiBridge.addTransport(&uartMidi);
    }
    midiBridge.addTransport(&connectivityManager);

    // 6. Start Transports (USB host rails switch PHY after usb_host_install)
    if (!usbMidi.begin(board)) {
        BRIDGE_LOG_LN("[USB] Host init failed — check VBUS / USB_DEV power");
    }
    bleMidi.begin(bridgeSystem.settings().bleDeviceName());
    if (bridgeSystem.settings().uartEnabled()) {
        uartMidi.begin(bridgeSystem.settings().uartBaudRate());
    }
    connectivityManager.begin();

    Serial.println("[SYSTEM] Deep Controller architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Periodic Maintenance
    board->task();
    inputManager.task(now);
    bridgeSystem.tick(now);
    
    usbMidi.task();
    bleMidi.task();
    uartMidi.task();
    connectivityManager.task();
    
    // UI Refresh
    if (canvas != nullptr) {
        BridgeUiDiagnostics diag;
        diag.usb = &usbMidi;
        diag.ble = &bleMidi;
        diag.usbStats = toUiStats(midiBridge.statsFor(TransportKind::kUsbHost));
        diag.bleStats = toUiStats(midiBridge.statsFor(TransportKind::kBle));
        diag.rtpStats = toUiStats(midiBridge.statsFor(TransportKind::kRtp));
        diag.uartStats = toUiStats(midiBridge.statsFor(TransportKind::kUart));
        diag.rtpConnected = connectivityManager.hasRtpSession();
        diag.uartEnabled = bridgeSystem.settings().uartEnabled();
        bridgeUi.setDiagnostics(diag);
        bridgeUi.refresh(now);
        canvas->flush();
    }
    
    vTaskDelay(pdMS_TO_TICKS(1));
}
