#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "bridge-s3 requires an ESP32-S3 board with native USB-OTG host support."
#endif

#include "src/hardware/Board.h"
#include "src/input/ButtonInput.h"
#include "src/network/BridgeLog.h"
#include "src/network/WifiDebugLogger.h"
#include "src/transports/usb/UsbMidiHost.h"
#include "src/transports/ble/BleMidiPeripheral.h"
#include "src/transports/uart/UartMidiPort.h"
#include "src/network/NetworkMidiTransport.h"
#include "src/app/BridgeController.h"
#include "src/midi/MidiBridge.h"
#include "src/ui/BridgeUi.h"
#include "src/config/BuildConfig.h"

// System Components — canvas allocated at static init (115 KB) while heap is clean.
static Board* board = createBoard();
static Arduino_Canvas* canvas = new Arduino_Canvas(240, 240, board->getDisplay());

static UsbMidiHost usbMidi;
static BleMidiPeripheral bleMidi;
static UartMidiPort uartMidi(Serial2, 48 /* RX */, 47 /* TX */);

#if ENABLE_BLE_DIAGNOSTICS
static uint32_t lastBleDiagMs = 0;
#endif

static bool usbHostStarted = false;
static uint32_t transportsStartedMs = 0;
static uint32_t bleSubscribedAtMs = 0;

static void startUsbHost()
{
    if (usbHostStarted) {
        return;
    }

    usbHostStarted = true;
    if (!usbMidi.begin(board)) {
        BRIDGE_LOG_LN("[USB] Host init failed — check VBUS / USB_DEV power");
    }
}

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
    wifiDebugLoggerBegin();
    
    // 1. Hardware Bootstrap
    if (!board->begin()) {
        Serial.println("[SYSTEM] Hardware initialization failed (LCD SPI).");
    } else {
        Serial.println("[SYSTEM] LCD panel initialized.");
    }
    
    // 2. System Controller (Brain)
    bridgeController.begin();
    
    // 3. UI and Graphics framebuffer (panel already initialized in board->begin)
    Serial.println("[SYSTEM] Initializing display canvas...");
    Serial.flush();
    if (canvas != nullptr && canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.printf("[SYSTEM] Canvas framebuffer OK (%u bytes free heap)\n", ESP.getFreeHeap());
        Serial.flush();
        bridgeUi.begin(canvas);
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
    buttonInput.mapButton("OK", board->getButtonPin("OK"));
    buttonInput.mapButton("UP", board->getButtonPin("UP"));
    buttonInput.mapButton("DOWN", board->getButtonPin("DOWN"));
    buttonInput.mapButton("MENU", board->getButtonPin("MENU"));

    buttonInput.onEvent("OK", [](ButtonInput::Event e){
        if (e == ButtonInput::Event::kTap) bridgeUi.confirmUnifiedView();
        else if (e == ButtonInput::Event::kLongHold) bridgeController.sendPanic();
    });
    buttonInput.onEvent("UP", [](ButtonInput::Event e){
        if (e == ButtonInput::Event::kTap) bridgeController.stepTranspose(1);
        else if (e == ButtonInput::Event::kLongHold) bridgeController.cycleMidiChannel();
    });
    buttonInput.onEvent("DOWN", [](ButtonInput::Event e){
        if (e == ButtonInput::Event::kTap) bridgeController.stepTranspose(-1);
        else if (e == ButtonInput::Event::kLongHold) networkMidi.startProvisioning();
    });
    buttonInput.onEvent("MENU", [](ButtonInput::Event e){
        if (e == ButtonInput::Event::kTap) bridgeController.cycleBacklightDim();
    });

    // 5. MIDI Hub Coordination
    midiBridge.begin(&bridgeUi, []() { return bridgeController.isPaused(); });
    midiBridge.setMidiEngine(&bridgeController.engine());
    midiBridge.addTransport(&usbMidi);
    midiBridge.addTransport(&bleMidi);
    if (bridgeController.settings().uartEnabled()) {
        midiBridge.addTransport(&uartMidi);
    }
    midiBridge.addTransport(&networkMidi);

    // 6. Start Transports. BLE starts first so a central can subscribe before
    // USB host muxing disrupts native USB CDC or BLE connection setup.
    bleMidi.begin(bridgeController.settings().bleDeviceName());
    if (bridgeController.settings().uartEnabled()) {
        uartMidi.begin(bridgeController.settings().uartBaudRate());
    }
    networkMidi.begin();
    transportsStartedMs = millis();

    Serial.println("[SYSTEM] Deep Controller architecture initialized.");
}

void loop()
{
    const uint32_t now = millis();
    
    // Periodic Maintenance
    board->task();
    buttonInput.task(now);
    bridgeController.tick(now);
    
    usbMidi.task();
    bleMidi.task();
    uartMidi.task();
    networkMidi.task();

    if (bleMidi.isSubscribed() && bleSubscribedAtMs == 0) {
        bleSubscribedAtMs = now;
    } else if (!bleMidi.isSubscribed()) {
        bleSubscribedAtMs = 0;
    }

    const bool bleSubscriptionDelayElapsed =
        bleSubscribedAtMs != 0 &&
        (now - bleSubscribedAtMs) >= USB_HOST_START_AFTER_BLE_SUBSCRIBE_DELAY_MS;

    if ((bleMidi.isSubscribed() && bleSubscriptionDelayElapsed) ||
        (USB_HOST_DEFER_UNTIL_BLE_SUBSCRIBE_MS == 0) ||
        (!usbHostStarted && (now - transportsStartedMs) >= USB_HOST_DEFER_UNTIL_BLE_SUBSCRIBE_MS)) {
        startUsbHost();
    }

#if ENABLE_BLE_DIAGNOSTICS
    if (bleMidi.isSubscribed() && (now - lastBleDiagMs) >= 1000) {
        lastBleDiagMs = now;
        char diag[180];
        snprintf(diag, sizeof(diag),
                 "DIAG USB stage=%.18s rails=%.30s seen=%u ready=%u canOut=%u vid=%04X pid=%04X if=%d in=%02X out=%02X raw=%lu midi=%lu drops=%lu mode=%c err=%.28s",
                 usbMidi.getHostStage(),
                 usbMidi.getRailConfig(),
                 usbMidi.hasSeenDevice() ? 1 : 0,
                 usbMidi.isConnected() ? 1 : 0,
                 usbMidi.canSend() ? 1 : 0,
                 usbMidi.getVendorId(),
                 usbMidi.getProductId(),
                 usbMidi.getMidiInterfaceNumber(),
                 usbMidi.getMidiInEndpoint(),
                 usbMidi.getMidiOutEndpoint(),
                 usbMidi.getRawUsbPacketsSeen(),
                 usbMidi.getDecodedMidiPacketsSeen(),
                 usbMidi.getDecodeDropCount(),
                 usbMidi.isVendorByteStreamMode() ? 'S' : 'P',
                 usbMidi.getLastError().c_str());
        bleMidi.sendDebugText(diag);
    }
#endif
    
    // UI Refresh
    if (canvas != nullptr) {
        BridgeUiDiagnostics diag;
        diag.usb = &usbMidi;
        diag.ble = &bleMidi;
        diag.usbStats = toUiStats(midiBridge.statsFor(MidiTransportKind::kUsbHost));
        diag.bleStats = toUiStats(midiBridge.statsFor(MidiTransportKind::kBle));
        diag.rtpStats = toUiStats(midiBridge.statsFor(MidiTransportKind::kRtp));
        diag.uartStats = toUiStats(midiBridge.statsFor(MidiTransportKind::kUart));
        diag.rtpConnected = networkMidi.hasRtpSession();
        diag.uartEnabled = bridgeController.settings().uartEnabled();
        bridgeUi.setDiagnostics(diag);
        bridgeUi.refresh(now);
        canvas->flush();
    }
    
    vTaskDelay(pdMS_TO_TICKS(1));
}
