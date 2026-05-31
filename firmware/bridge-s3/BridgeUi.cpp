#include "BridgeUi.h"

#include "BLEConnection.h"
#include "BridgeSettings.h"
#include "MidiBridge.h"
#include "MidiCodec.h"
#include "RTPMidiConfig.h"
#include "USBConnection.h"
#include "NetworkServices.h"
#include "animation/BongoCat.h"

BridgeUi bridgeUi;

namespace {

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

#ifndef BOARD_BTN_UP
#define BOARD_BTN_UP 10
#endif

#ifndef BOARD_BTN_DOWN
#define BOARD_BTN_DOWN 11
#endif

#ifndef BOARD_BTN_MENU
#define BOARD_BTN_MENU 14
#endif

constexpr uint16_t kShortPressMaxMs = 800;
constexpr uint16_t kOkPanicHoldMs = 1000;
constexpr uint16_t kOkPauseHoldMs = 2500;
constexpr uint16_t kMenuLongHoldMs = 1000;
constexpr uint16_t kMenuWifiSetupHoldMs = 4000;
constexpr uint32_t kToastDurationMs = 2000;

bool isButtonPressed(int8_t pin)
{
    return pin >= 0 && digitalRead(pin) == LOW;
}

}  // namespace

constexpr int16_t kSidebarW = 54;
constexpr int16_t kKeyboardX = kSidebarW + 10;
constexpr int16_t kKeyboardW = 240 - kKeyboardX - 10;
constexpr int16_t kKeyboardY = BongoCatDisplay::kOriginY + 128 - 14;
constexpr int16_t kKeyboardH = 12;
constexpr uint8_t kKeyboardFirstNote = 48;  // C3
constexpr int16_t kKeyboardKeyCount = 21;

#ifndef BACKLIGHT_DIM_LEVEL
#define BACKLIGHT_DIM_LEVEL 24
#endif

static const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#ifndef BATT_SENSE_PIN
#define BATT_SENSE_PIN 1
#endif

void BridgeUi::begin(Arduino_GFX* display, int16_t blPin)
{
    gfx = display;
    backlightPin = blPin;
    lastActivityMs_ = millis();
    initBoardButtons();

    if (BATT_SENSE_PIN >= 0) {
        analogReadResolution(12);
    }

    if (backlightPin >= 0) {
        ledcAttach(static_cast<uint8_t>(backlightPin), 5000, 8);
        setBacklight(255);
    }
}

void BridgeUi::setBle(BLEConnection* connection)
{
    ble = connection;
}

void BridgeUi::setUsbMidi(USBConnection* usb)
{
    usb_ = usb;
}

void BridgeUi::setMidiBridge(MidiBridge* bridge)
{
    midiBridge_ = bridge;
}

void BridgeUi::setBongoCat(BongoCatDisplay* bongoCat)
{
    bongoCat_ = bongoCat;
}

void BridgeUi::notifyUsbStatus(bool connected)
{
    usbConnected_ = connected;
}

void BridgeUi::notifyBleStatus(bool connected)
{
    bleConnected_ = connected;
}

void BridgeUi::notifyRawMidiEvent(const uint8_t* data, size_t length)
{
    uint8_t usbPacket[4] = {0, 0, 0, 0};
    if (length >= 1) usbPacket[1] = data[0];
    if (length >= 2) usbPacket[2] = data[1];
    if (length >= 3) usbPacket[3] = data[2];
    notifyMidiEvent(usbPacket);
}

void BridgeUi::notifyMidiEvent(const uint8_t* data)
{
    const uint8_t status = data[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;
    const uint8_t data1 = data[2];
    const uint8_t data2 = data[3];
    char noteBuffer[12] = {0};
    char logLine[32] = {0};

    if ((messageType == 0x90 && data2 > 0) || messageType == 0x80 || (messageType == 0x90 && data2 == 0)) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        noteEventsSeen_++;
        onNoteEvent(noteOn, data1, data2);
        MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer));
        snprintf(lastMidiText_, sizeof(lastMidiText_), "%s %s ch%u v%u", noteOn ? "On" : "Off", noteBuffer, channel, data2);
        snprintf(logLine, sizeof(logLine), "%-3s %s (v%u)", noteOn ? "ON" : "OFF", noteBuffer, data2);
        addLogEntry(logLine, noteOn ? RGB565_LIME : RGB565_DARKGRAY);
    } else if (messageType == 0xB0) {
        onControlChange(data1, data2);
        snprintf(lastMidiText_, sizeof(lastMidiText_), "CC %u=%u ch%u", data1, data2, channel);
        snprintf(logLine, sizeof(logLine), "CC %u=%u", data1, data2);
        addLogEntry(logLine, RGB565_GOLD);
    } else if (messageType == 0xE0) {
        const uint16_t bend = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
        snprintf(lastMidiText_, sizeof(lastMidiText_), "Bend %u ch%u", bend, channel);
        snprintf(logLine, sizeof(logLine), "Bend %u", bend);
        addLogEntry(logLine, RGB565_CYAN);
    } else {
        snprintf(lastMidiText_, sizeof(lastMidiText_), "%s ch%u", MidiCodec::statusName(status), channel);
        addLogEntry(MidiCodec::statusName(status), RGB565_LIGHTGRAY);
    }
    lastMidiMs_ = millis();
    touchActivity(lastMidiMs_);
}

void BridgeUi::applySavedDisplayMode(uint8_t modeIndex)
{
    if (modeIndex >= static_cast<uint8_t>(DisplayMode::kModeCount)) {
        modeIndex = 0;
    }
    displayMode_ = static_cast<DisplayMode>(modeIndex);
}

uint32_t BridgeUi::backlightDimMs() const
{
    return bridgeSettings.backlightDimMs();
}

void BridgeUi::touchActivity(uint32_t nowMs)
{
    lastActivityMs_ = nowMs;
    if (backlightLevel_ < 200) {
        setBacklight(255);
    }
}

void BridgeUi::setBacklight(uint8_t level)
{
    backlightLevel_ = level;
    if (backlightPin >= 0) {
        ledcWrite(static_cast<uint8_t>(backlightPin), level);
    }
}

void BridgeUi::onNoteEvent(bool noteOn, uint8_t note, uint8_t velocity)
{
    lastVelocity_ = velocity;
    lastNote_ = note;
    refreshLastNoteLabel();

    if (note < 128) {
        if (noteOn && !heldNotes[note]) {
            heldNotes[note] = true;
            heldCount_++;
        } else if (!noteOn && heldNotes[note]) {
            heldNotes[note] = false;
            if (heldCount_ > 0) {
                heldCount_--;
            }
        }
    }

    if (noteOn) {
        noteOnTimes[noteOnHead_ % 32] = millis();
        noteOnHead_++;
    }
}

void BridgeUi::onControlChange(uint8_t cc, uint8_t value)
{
    if (cc == 64) {
        sustainDown_ = value >= 64;
    }
}

void BridgeUi::refreshLastNoteLabel()
{
    const int octave = static_cast<int>(lastNote_ / 12) - 1;
    snprintf(lastNoteLabel_, sizeof(lastNoteLabel_), "%s%d", kNoteNames[lastNote_ % 12], octave);
}

void BridgeUi::addLogEntry(const char* text, uint16_t color)
{
    if (text == nullptr) return;
    
    strncpy(logEntries_[logHead_].text, text, 31);
    logEntries_[logHead_].text[31] = '\0';
    logEntries_[logHead_].color = color;
    logEntries_[logHead_].timestamp = millis();
    
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
}

uint8_t BridgeUi::heldNoteCount() const
{
    return heldCount_;
}

uint16_t BridgeUi::notesPerMinute() const
{
    return notesPerMinute_;
}

void BridgeUi::updateNotesPerMinute(uint32_t nowMs)
{
    uint16_t count = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (noteOnTimes[i] != 0 && nowMs - noteOnTimes[i] < 60000) {
            count++;
        }
    }
    notesPerMinute_ = count;
}

const char* BridgeUi::displayModeName() const
{
    switch (displayMode_) {
        case DisplayMode::kPerformance:
            return "PERF";
        case DisplayMode::kMinimal:
            return "MIN";
        case DisplayMode::kStage:
            return "STAGE";
        default:
            return "FULL";
    }
}

void BridgeUi::cycleDisplayMode()
{
    const uint8_t next = (static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount);
    displayMode_ = static_cast<DisplayMode>(next);
    bridgeSettings.saveDisplayMode(static_cast<uint8_t>(displayMode_));
    Serial.printf("[UI] Display mode: %s\n", displayModeName());
}

void BridgeUi::toggleBridgePaused()
{
    bridgePaused_ = !bridgePaused_;
    Serial.printf("[UI] MIDI bridge %s\n", bridgePaused_ ? "PAUSED" : "running");
}

void BridgeUi::sendAllNotesOff()
{
    if (ble == nullptr || !ble->isConnected()) {
        Serial.println("[UI] Panic ignored (BLE not connected)");
        return;
    }

    for (uint8_t ch = 0; ch < 16; ch++) {
        uint8_t packet[5] = {0};
        size_t len = 0;
        MidiCodec::appendBleTimestamp(packet, &len, static_cast<uint16_t>(millis()));
        packet[len++] = static_cast<uint8_t>(0xB0 | ch);
        packet[len++] = 123;
        packet[len++] = 0;
        ble->sendMidi(packet, len);

        MidiCodec::appendBleTimestamp(packet, &len, static_cast<uint16_t>(millis()));
        packet[len++] = static_cast<uint8_t>(0xB0 | ch);
        packet[len++] = 120;
        packet[len++] = 0;
        ble->sendMidi(packet, len);
    }

    for (uint8_t n = 0; n < 128; n++) {
        heldNotes[n] = false;
    }
    heldCount_ = 0;
    sustainDown_ = false;
    Serial.println("[UI] Sent All Notes Off to BLE");
}

bool BridgeUi::shouldDrawFullMetrics() const
{
    return displayMode_ == DisplayMode::kFull;
}

bool BridgeUi::shouldDrawStatusPanel() const
{
    return displayMode_ != DisplayMode::kStage;
}

void BridgeUi::showToast(const char* text, uint32_t nowMs)
{
    if (text == nullptr || text[0] == '\0') {
        toastUntilMs_ = 0;
        toastText_[0] = '\0';
        return;
    }
    strncpy(toastText_, text, sizeof(toastText_) - 1);
    toastText_[sizeof(toastText_) - 1] = '\0';
    toastUntilMs_ = nowMs + kToastDurationMs;
}

void BridgeUi::initBoardButtons()
{
    okButton_.pin = BOOT_BUTTON_PIN;
    upButton_.pin = BOARD_BTN_UP;
    downButton_.pin = BOARD_BTN_DOWN;
    menuButton_.pin = BOARD_BTN_MENU;

    const int8_t pins[] = {okButton_.pin, upButton_.pin, downButton_.pin, menuButton_.pin};
    for (int8_t pin : pins) {
        if (pin >= 0) {
            pinMode(pin, INPUT_PULLUP);
        }
    }
}

void BridgeUi::handleBoardButtons(uint32_t nowMs)
{
    const bool okPressed = isButtonPressed(okButton_.pin);
    if (okPressed && !okButton_.down) {
        okButton_.down = true;
        okButton_.downMs = nowMs;
        okButton_.actionFired = false;
        pauseToggleFired_ = false;
    }

    if (!okPressed && okButton_.down) {
        if (!okButton_.actionFired && nowMs - okButton_.downMs < kShortPressMaxMs) {
            cycleDisplayMode();
            showToast(displayModeName(), nowMs);
        }
        okButton_.down = false;
        okButton_.actionFired = false;
        pauseToggleFired_ = false;
        return;
    }

    if (okPressed && okButton_.down && nowMs - okButton_.downMs >= kOkPanicHoldMs && !okButton_.actionFired) {
        okButton_.actionFired = true;
        sendAllNotesOff();
        showToast("PANIC", nowMs);
    }

    if (okPressed && okButton_.down && nowMs - okButton_.downMs >= kOkPauseHoldMs && !pauseToggleFired_) {
        pauseToggleFired_ = true;
        toggleBridgePaused();
        okButton_.actionFired = true;
        showToast(bridgePaused_ ? "PAUSED" : "RUNNING", nowMs);
    }

    const bool upPressed = isButtonPressed(upButton_.pin);
    if (upPressed && !upButton_.down) {
        upButton_.down = true;
        upButton_.downMs = nowMs;
        upButton_.actionFired = false;
    }
    if (!upPressed && upButton_.down) {
        if (!upButton_.actionFired && nowMs - upButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.stepTranspose(1);
            char toast[24];
            snprintf(toast, sizeof(toast), "Transpose %+d", bridgeSettings.transposeSemitones());
            showToast(toast, nowMs);
        }
        upButton_.down = false;
        upButton_.actionFired = false;
    }

    const bool downPressed = isButtonPressed(downButton_.pin);
    if (downPressed && !downButton_.down) {
        downButton_.down = true;
        downButton_.downMs = nowMs;
        downButton_.actionFired = false;
    }
    if (!downPressed && downButton_.down) {
        if (!downButton_.actionFired && nowMs - downButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.stepTranspose(-1);
            char toast[24];
            snprintf(toast, sizeof(toast), "Transpose %+d", bridgeSettings.transposeSemitones());
            showToast(toast, nowMs);
        }
        downButton_.down = false;
        downButton_.actionFired = false;
    }

    const bool menuPressed = isButtonPressed(menuButton_.pin);
    if (menuPressed && !menuButton_.down) {
        menuButton_.down = true;
        menuButton_.downMs = nowMs;
        menuButton_.actionFired = false;
        menuWifiSetupFired_ = false;
    }

    if (!menuPressed && menuButton_.down) {
        if (!menuButton_.actionFired && nowMs - menuButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.cycleMidiChannelFilter();
            char toast[24];
            if (bridgeSettings.midiChannelFilter() == 0) {
                snprintf(toast, sizeof(toast), "MIDI: all ch");
            } else {
                snprintf(toast, sizeof(toast), "MIDI: ch%u only", bridgeSettings.midiChannelFilter());
            }
            showToast(toast, nowMs);
        }
        menuButton_.down = false;
        menuButton_.actionFired = false;
        menuWifiSetupFired_ = false;
        return;
    }

    if (menuPressed && menuButton_.down && nowMs - menuButton_.downMs >= kMenuWifiSetupHoldMs && !menuWifiSetupFired_) {
        menuWifiSetupFired_ = true;
        menuButton_.actionFired = true;
#if ENABLE_RTP_MIDI
        networkServices.enterSetupMode();
#endif
        showToast("WiFi SETUP", nowMs);
        return;
    }

    if (menuPressed && menuButton_.down && nowMs - menuButton_.downMs >= kMenuLongHoldMs && !menuButton_.actionFired) {
        menuButton_.actionFired = true;
        bridgeSettings.cycleBacklightDim();
        char toast[24];
        if (bridgeSettings.backlightDimMs() == 0) {
            snprintf(toast, sizeof(toast), "Dim: never");
        } else {
            snprintf(toast, sizeof(toast), "Dim: %lus", bridgeSettings.backlightDimMs() / 1000);
        }
        showToast(toast, nowMs);
    }
}

void BridgeUi::drawToast(uint32_t nowMs)
{
    if (gfx == nullptr || nowMs >= toastUntilMs_ || toastText_[0] == '\0') {
        return;
    }

    constexpr int16_t toastY = 218;
    gfx->fillRoundRect(kSidebarW + 10, toastY, 160, 18, 6, RGB565(24, 24, 48));
    gfx->drawRoundRect(kSidebarW + 10, toastY, 160, 18, 6, RGB565_CYAN);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(kSidebarW + 18, toastY + 5);
    gfx->print(toastText_);
}

void BridgeUi::tick(uint32_t nowMs)
{
    handleBoardButtons(nowMs);
    updateNotesPerMinute(nowMs);

    // Update battery sensing occasionally
    static uint32_t lastSenseMs = 0;
    if (nowMs - lastSenseMs > 5000) {
        lastSenseMs = nowMs;
        if (BATT_SENSE_PIN >= 0) {
            const int raw = analogRead(BATT_SENSE_PIN);
            batteryVoltage_ = (raw / 4095.0f) * 3.3f * 2.0f;
            isUsbPowered_ = batteryVoltage_ > 4.4f; 
        }
    }

    const uint32_t dimMs = backlightDimMs();
    if (backlightPin >= 0 && dimMs > 0) {
        if (nowMs - lastActivityMs_ > dimMs) {
            if (backlightLevel_ > BACKLIGHT_DIM_LEVEL) {
                setBacklight(BACKLIGHT_DIM_LEVEL);
            }
        }
    }
}

void BridgeUi::drawMiniKeyboard()
{
    if (gfx == nullptr || displayMode_ == DisplayMode::kMinimal || displayMode_ == DisplayMode::kStage) {
        return;
    }

    const int16_t keyW = kKeyboardW / kKeyboardKeyCount;
    for (uint8_t i = 0; i < kKeyboardKeyCount; i++) {
        const uint8_t note = kKeyboardFirstNote + i;
        const bool blackKey = ((i + 1) % 12 == 2 || (i + 1) % 12 == 4 || (i + 1) % 12 == 7 ||
                               (i + 1) % 12 == 9 || (i + 1) % 12 == 11);
        const bool held = note < 128 && heldNotes[note];
        const int16_t x = kKeyboardX + i * keyW;
        const uint16_t fill = held ? RGB565(80, 220, 120) : (blackKey ? RGB565(20, 20, 28) : RGB565(50, 50, 68));
        gfx->fillRect(x + 1, kKeyboardY + 1, keyW - 2, kKeyboardH - 2, fill);
        gfx->drawRect(x, kKeyboardY, keyW, kKeyboardH, held ? RGB565_LIME : RGB565(32, 32, 48));
    }
}

void BridgeUi::drawVelocityBar()
{
    if (gfx == nullptr || displayMode_ == DisplayMode::kMinimal || displayMode_ == DisplayMode::kStage) {
        return;
    }

    constexpr int16_t barX = 240 - 50;
    constexpr int16_t barY = 14;
    constexpr int16_t barW = 36;
    constexpr int16_t barH = 8;

    gfx->drawRect(barX, barY, barW, barH, RGB565(48, 48, 64));
    const int16_t fillW = (static_cast<int16_t>(lastVelocity_) * (barW - 2)) / 127;
    if (fillW > 0) {
        const uint16_t color = lastVelocity_ > 100 ? RGB565(255, 96, 64) : RGB565(64, 160, 255);
        gfx->fillRect(barX + 1, barY + 1, fillW, barH - 2, color);
    }
}

void BridgeUi::drawStatusChips(uint32_t nowMs)
{
    if (gfx == nullptr) {
        return;
    }

    // Hardware status bar (Top Right)
    const uint16_t battColor = batteryVoltage_ > 3.6f ? RGB565_LIME : (batteryVoltage_ > 3.4f ? RGB565_GOLD : RGB565_RED);
    gfx->drawRect(210, 14, 18, 8, RGB565_DARKGRAY);
    gfx->fillRect(228, 16, 2, 4, RGB565_DARKGRAY);
    
    float level = (batteryVoltage_ - 3.3f) / (4.2f - 3.3f);
    if (level > 1.0f) level = 1.0f;
    if (level < 0.0f) level = 0.0f;
    
    if (isUsbPowered_) {
        gfx->fillRect(212, 16, 14, 4, RGB565_CYAN);
    } else {
        gfx->fillRect(212, 16, static_cast<int16_t>(level * 14), 4, battColor);
    }

    constexpr int16_t chipY = 14;
    if (sustainDown_) {
        gfx->fillRoundRect(160, chipY, 34, 12, 4, RGB565(64, 32, 0));
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_GOLD);
        gfx->setCursor(164, chipY + 2);
        gfx->print("SUS");
    }

    if (bridgePaused_) {
        gfx->fillRoundRect(80, BongoCatDisplay::kOriginY + 48, 100, 22, 6, RGB565(96, 16, 16));
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(95, BongoCatDisplay::kOriginY + 55);
        gfx->print("PAUSED");
    }
}

void BridgeUi::drawOverlays(uint32_t nowMs)
{
    touchActivity(nowMs);
    drawVelocityBar();
    drawMiniKeyboard();
    drawStatusChips(nowMs);
    drawToast(nowMs);

    if (displayMode_ == DisplayMode::kPerformance && gfx != nullptr) {
        gfx->setTextSize(2);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(kSidebarW + 10, BongoCatDisplay::kStatusTop - 18);
        gfx->print(lastNoteLabel_);
    }
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return; // Responsive 20fps
    lastRefreshMs_ = nowMs;

    const bool usbOk = usb_ && usb_->isConnected();
    const bool bleOk = ble && ble->isConnected();

    gfx->fillScreen(RGB565_BLACK);

    // Sidebar: High-Contrast Technical Slate
    constexpr uint16_t kSideColor = RGB565(15, 15, 20);
    constexpr uint16_t kBorderColor = RGB565(40, 40, 50);
    gfx->fillRect(0, 0, kSidebarW, 240, kSideColor);
    gfx->drawFastVLine(kSidebarW, 0, 240, kBorderColor);

    // Labels & Controls
    gfx->setTextSize(1);
    auto drawMetric = [&](int16_t y, const char* label, const char* value, uint16_t color) {
        gfx->setTextColor(RGB565(80, 80, 100));
        gfx->setCursor(8, y); gfx->print(label);
        gfx->setTextColor(color);
        gfx->setCursor(10, y + 10); gfx->setTextSize(2); gfx->print(value);
        gfx->setTextSize(1);
    };

    drawMetric(12, "TRANS", 
               String(bridgeSettings.transposeSemitones() > 0 ? "+" : "") + String(bridgeSettings.transposeSemitones()), 
               bridgeSettings.transposeSemitones() != 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    
    char chanBuf[8];
    if (bridgeSettings.midiChannelFilter() == 0) strcpy(chanBuf, "ALL"); else sprintf(chanBuf, "CH%u", bridgeSettings.midiChannelFilter());
    drawMetric(60, "FILTER", chanBuf, bridgeSettings.midiChannelFilter() > 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    
    drawMetric(108, "MODE", displayModeName(), RGB565_CYAN);

    // Status Chips (Bottom of sidebar)
    const float v = batteryVoltage();
    const uint16_t bColor = v > 3.7f ? RGB565_LIME : (v > 3.4f ? RGB565_GOLD : RGB565_RED);
    gfx->drawRect(12, 210, 24, 12, RGB565(60, 60, 70));
    gfx->fillRect(36, 213, 2, 6, RGB565(60, 60, 70));
    float lvl = (v - 3.3f) / (4.2f - 3.3f);
    if (lvl > 1.0f) lvl = 1.0f; else if (lvl < 0.0f) lvl = 0.0f;
    gfx->fillRect(14, 212, (int)(lvl * 20), 8, isUsbPowered() ? RGB565_CYAN : bColor);

    // Main Console Area
    const int16_t mainX = kSidebarW + 12;
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(mainX, 10);
    gfx->print("MIDI STATION");
    gfx->drawFastHLine(mainX, 28, 240 - mainX - 12, kBorderColor);

    if (shouldDrawStatusPanel()) {
        // Hardware Sync Block
        auto drawSync = [&](int16_t x, int16_t y, const char* label, bool ok) {
            uint16_t color = ok ? RGB565_LIME : RGB565(100, 100, 0);
            gfx->drawRect(x, y, 78, 30, RGB565(30, 30, 40));
            gfx->setTextSize(1);
            gfx->setTextColor(RGB565(120, 120, 130));
            gfx->setCursor(x + 5, y + 4); gfx->print(label);
            gfx->setTextColor(color);
            gfx->setCursor(x + 5, y + 16);
            gfx->print(ok ? "[ ONLINE ]" : "[ OFFLINE ]");
        };
        drawSync(mainX, 36, "USB HOST", usbOk);
        drawSync(mainX + 84, 36, "BLE MIDI", bleOk);

        // Terminal Log
        const int16_t logY = 78;
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setCursor(mainX, logY);
        gfx->print("ACTIVITY STREAM");
        
        // Terminal Window
        gfx->fillRect(mainX, logY + 12, 162, 66, RGB565(10, 10, 15));
        gfx->drawRect(mainX, logY + 12, 162, 66, RGB565(25, 25, 35));
        
        // System Heartbeat (Professional pulse in corner of terminal)
        const uint16_t pulseColor = (nowMs / 1000) % 2 == 0 ? RGB565(40, 40, 60) : RGB565(20, 20, 30);
        gfx->fillRect(mainX + 155, logY + 15, 4, 4, pulseColor);

        for (uint8_t i = 0; i < logCount(); i++) {
            const auto* e = &logEntries()[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
            const int16_t eY = logY + 17 + (i * 12);
            gfx->setTextColor(e->color);
            gfx->setCursor(mainX + 6, eY);
            gfx->printf("> %s", e->text);
            if (nowMs - e->timestamp < 400) {
                gfx->fillRect(mainX + 154, eY + 1, 3, 7, e->color);
            }
        }

        // Live Counters
        if (midiBridge_) {
            const auto& c = midiBridge_->counters();
            gfx->setTextColor(RGB565(60, 60, 75));
            gfx->setCursor(mainX, 224);
            gfx->printf("IN: %u  OUT: %u  ERR: %u", c.usbPacketsSeen, c.blePacketsSent, c.blePacketsSkipped);
        }
    }

    // Animation Layer
    const bool active = lastMidiMs_ > 0 && nowMs - lastMidiMs_ < 400;
    if (bongoCat_) {
        bongoCat_->update(nowMs, active, noteEventsSeen_);
        // Center cat in the remaining space (240 - sidebar_w = 186)
        // Cat is 128px wide. (186 - 128) / 2 = 29px from sidebar.
        // mainX is sidebar_w + 12 = 66. 66 + 29 = 95.
        bongoCat_->draw(gfx, 95); 
    }

    drawOverlays(nowMs);
}
