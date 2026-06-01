// ESP32_Host_MIDI USB host -> BLE with on-screen diagnostics (ESP32-S3-USB-OTG).
#include <Arduino.h>
#include <ESP32_Host_MIDI.h>
#include <USBConnection.h>
#include <BLEConnection.h>

#include "Board.h"
#include "DiagDisplay.h"

USBConnection usbHost;
BLEConnection bleHost;

static Board* board = createBoard();
static Arduino_Canvas* canvas = nullptr;

static volatile uint32_t gUsbIn = 0;
static volatile uint32_t gBleOutOk = 0;
static volatile uint32_t gBleOutSkip = 0;
static char gLastEvent[40] = {};
static char gLastResult[24] = {};

static void formatMidiEvent(const uint8_t* raw, size_t rawLen, const uint8_t* midiData, char* buf, size_t bufLen) {
    const uint8_t* m = (rawLen >= 4) ? midiData : raw;
    const size_t mLen = (rawLen >= 4) ? (rawLen - 1) : rawLen;
    if (mLen < 1) {
        snprintf(buf, bufLen, "empty");
        return;
    }

    const uint8_t status = m[0];
    const uint8_t type = status & 0xF0;
    const uint8_t ch = (status & 0x0F) + 1;

    if (type == 0x90 && mLen >= 3) {
        char note[8];
        MIDIHandler::noteWithOctave(m[1], note, sizeof(note));
        snprintf(buf, bufLen, "ON %s v%u ch%u", note, m[2], ch);
    } else if (type == 0x80 && mLen >= 3) {
        char note[8];
        MIDIHandler::noteWithOctave(m[1], note, sizeof(note));
        snprintf(buf, bufLen, "OFF %s ch%u", note, ch);
    } else if (type == 0xB0 && mLen >= 3) {
        snprintf(buf, bufLen, "CC%u=%u ch%u", m[1], m[2], ch);
    } else {
        snprintf(buf, bufLen, "0x%02X ch%u", status, ch);
    }
}

static void forwardUsbToBle(const uint8_t* raw, size_t rawLen, const uint8_t* midiData) {
    gUsbIn++;

    char eventLine[40];
    formatMidiEvent(raw, rawLen, midiData, eventLine, sizeof(eventLine));
    strncpy(gLastEvent, eventLine, sizeof(gLastEvent) - 1);
    gLastEvent[sizeof(gLastEvent) - 1] = '\0';

    if (!bleHost.isConnected()) {
        gBleOutSkip++;
        strncpy(gLastResult, "BLE not linked", sizeof(gLastResult) - 1);
        diagDisplay.pushLog(eventLine, RGB565_ORANGE);
        return;
    }

    const uint8_t* out = (rawLen >= 4) ? midiData : raw;
    const size_t outLen = (rawLen >= 4) ? (rawLen - 1) : rawLen;
    if (outLen < 2) {
        gBleOutSkip++;
        strncpy(gLastResult, "bad len", sizeof(gLastResult) - 1);
        return;
    }

    if (bleHost.sendMidiMessage(out, outLen)) {
        gBleOutOk++;
        strncpy(gLastResult, "sent BLE", sizeof(gLastResult) - 1);
        diagDisplay.pushLog(eventLine, RGB565_LIME);
    } else {
        gBleOutSkip++;
        strncpy(gLastResult, "BLE send fail", sizeof(gLastResult) - 1);
        diagDisplay.pushLog(eventLine, RGB565_RED);
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 5000) {
        delay(10);
    }

    Serial.println("\n[HostMIDI] USB host -> BLE + display");

    if (!board->begin()) {
        Serial.println("[HostMIDI] LCD init failed");
    }

    canvas = new Arduino_Canvas(240, 240, board->getDisplay());
    if (canvas != nullptr && canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        board->setBacklight(255);
        diagDisplay.begin(canvas);
        diagDisplay.pushLog("Display OK", RGB565_CYAN);
        diagDisplay.refresh(millis());
        canvas->flush();
        Serial.println("[HostMIDI] Display ready");
    }

    midiHandler.addTransport(&usbHost);
    if (!usbHost.begin()) {
        Serial.println("[HostMIDI] USB host init failed");
        diagDisplay.pushLog("USB init FAIL", RGB565_RED);
    } else {
        board->enableUsbHostPower();
        Serial.println("[HostMIDI] USB host + power rails OK");
        diagDisplay.pushLog("USB host OK", RGB565_LIME);
    }

    bleHost.begin("Piano BLE Bridge");
    midiHandler.setRawMidiCallback(forwardUsbToBle);
    midiHandler.begin();

    diagDisplay.pushLog("BLE: Piano BLE Bridge", RGB565_CYAN);
    Serial.println("[HostMIDI] Ready");
}

void loop() {
    const uint32_t now = millis();
    midiHandler.task();

    DiagStats stats;
    stats.usbIn = gUsbIn;
    stats.bleOutOk = gBleOutOk;
    stats.bleOutSkip = gBleOutSkip;
    stats.usbHostReady = usbHost.isConnected();
    stats.bleConnected = bleHost.isConnected();
    strncpy(stats.lastEvent, gLastEvent, sizeof(stats.lastEvent) - 1);
    strncpy(stats.lastResult, gLastResult, sizeof(stats.lastResult) - 1);
    diagDisplay.setStats(stats);

    if (canvas != nullptr) {
        diagDisplay.refresh(now);
        canvas->flush();
    }

    delay(1);
}
