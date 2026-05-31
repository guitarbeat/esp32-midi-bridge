#include "BridgeUi.h"

#include "BLEConnection.h"
#include "Board.h"
#include "BridgeSettings.h"
#include "MidiBridge.h"
#include "MidiCodec.h"
#include "MidiEngine.h"
#include "RTPMidiConfig.h"
#include "USBConnection.h"
#include "ConnectivityManager.h"
#include "animation/BongoCat.h"

BridgeUi bridgeUi;

void BridgeUi::begin(Arduino_GFX* gfx_ptr)
{
    gfx = gfx_ptr;
    lastActivityMs_ = millis();
}

void BridgeUi::setBle(BLEConnection* connection) { ble = connection; }
void BridgeUi::setUsbMidi(USBConnection* usb) { usb_ = usb; }
void BridgeUi::setMidiBridge(MidiBridge* bridge) { midiBridge_ = bridge; }
void BridgeUi::setMidiEngine(MidiEngine* engine) { engine_ = engine; }
void BridgeUi::setBongoCat(BongoCatDisplay* bongoCat) { bongoCat_ = bongoCat; }
void BridgeUi::setBoard(Board* board) { board_ = board; }

void BridgeUi::notifyUsbStatus(bool connected) { usbConnected_ = connected; }
void BridgeUi::notifyBleStatus(bool connected) { bleConnected_ = connected; }

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

    if (messageType == 0x90 || messageType == 0x80) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer));
        snprintf(logLine, sizeof(logLine), "%-3s %s (v%u)", noteOn ? "ON" : "OFF", noteBuffer, data2);
        addLogEntry(logLine, noteOn ? RGB565_LIME : RGB565_DARKGRAY);
    } else if (messageType == 0xB0) {
        snprintf(logLine, sizeof(logLine), "CC %u=%u", data1, data2);
        addLogEntry(logLine, RGB565_GOLD);
    } else if (messageType == 0xE0) {
        addLogEntry("Pitch Bend", RGB565_CYAN);
    } else {
        addLogEntry(MidiCodec::statusName(status), RGB565_LIGHTGRAY);
    }
    lastMidiMs_ = millis();
    lastActivityMs_ = lastMidiMs_;
    setBacklight(255);
}

void BridgeUi::applySavedDisplayMode(uint8_t modeIndex)
{
    if (modeIndex >= static_cast<uint8_t>(DisplayMode::kModeCount)) modeIndex = 0;
    displayMode_ = static_cast<DisplayMode>(modeIndex);
}

uint32_t BridgeUi::backlightDimMs() const { return bridgeSettings.backlightDimMs(); }

void BridgeUi::addLogEntry(const char* text, uint16_t color)
{
    if (text == nullptr) return;
    MidiLogEntry& e = logEntries_[logHead_];
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    e.color = color;
    e.timestamp = millis();
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
}

void BridgeUi::tick(uint32_t nowMs)
{
    if (engine_) engine_->tick(nowMs);

    const uint32_t dimMs = backlightDimMs();
    if (board_ && dimMs > 0 && nowMs - lastActivityMs_ > dimMs) {
        setBacklight(40); // Dim level
    }
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr || engine_ == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return;
    lastRefreshMs_ = nowMs;

    const bool usbOk = usb_ && usb_->isConnected();
    const bool bleOk = ble && ble->isConnected();
    const auto& me = engine_->state();

    gfx->fillScreen(RGB565_BLACK);

    // 1. Industrial Header
    constexpr uint16_t kHeaderColor = RGB565(20, 20, 30);
    constexpr uint16_t kAccentColor = RGB565_CYAN;
    gfx->fillRect(0, 0, 240, 32, kHeaderColor);
    gfx->drawFastHLine(0, 32, 240, RGB565(60, 60, 80));

    gfx->setTextSize(1);
    gfx->setTextColor(bleOk ? kAccentColor : RGB565_DARKGRAY);
    gfx->setCursor(10, 11); gfx->printf("BLE [%s]", bleOk ? "ON" : "OFF");
    
    gfx->setTextColor(connectivityManager.isConnected() ? kAccentColor : RGB565_DARKGRAY);
    gfx->setCursor(75, 11); gfx->printf("WIFI [%s]", connectivityManager.isConnected() ? "RTP" : "---");

    if (board_) {
        const float v = board_->getBatteryVoltage();
        const uint16_t bColor = v > 3.7f ? RGB565_LIME : (v > 3.4f ? RGB565_GOLD : RGB565_RED);
        gfx->drawRect(185, 11, 22, 10, RGB565_DARKGRAY);
        gfx->fillRect(207, 13, 2, 6, RGB565_DARKGRAY);
        float lvl = (v - 3.3f) / 0.9f;
        if (lvl > 1.0f) lvl = 1.0f; else if (lvl < 0.0f) lvl = 0.0f;
        gfx->fillRect(187, 12, (int)(lvl * 18), 8, board_->isUsbPowered() ? kAccentColor : bColor);
        gfx->setCursor(215, 11); gfx->setTextColor(RGB565_LIGHTGRAY); gfx->printf("%.1fV", v);
    }

    // 2. Information Cards
    auto drawCard = [&](int16_t x, int16_t y, int16_t w, int16_t h, const char* label, const char* value, uint16_t valColor) {
        gfx->drawRoundRect(x, y, w, h, 6, RGB565(40, 40, 50));
        gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setCursor(x + 8, y + 6); gfx->print(label);
        gfx->setTextColor(valColor); gfx->setTextSize(2);
        gfx->setCursor(x + 8, y + 18); gfx->print(value);
        gfx->setTextSize(1);
    };

    const char* usbName = (usb_ && usbOk) ? usb_->getDeviceName().substring(0, 12).c_str() : "DISCONNECTED";
    drawCard(10, 42, 140, 44, "USB PIANO", usbName, usbOk ? RGB565_LIME : RGB565_GOLD);
    
    char transBuf[8]; snprintf(transBuf, sizeof(transBuf), "%+d", engine_->transpose());
    drawCard(158, 42, 72, 44, "TRANS", transBuf, engine_->transpose() != 0 ? RGB565_GOLD : RGB565_LIGHTGRAY);

    // 3. Activity Console
    const int16_t logY = 96;
    gfx->setTextColor(RGB565(100, 100, 120));
    gfx->setCursor(10, logY); gfx->print("LIVE MONITOR");
    gfx->fillRect(10, logY + 12, 220, 54, RGB565(10, 10, 15));
    gfx->drawRect(10, logY + 12, 220, 54, RGB565(30, 30, 40));

    for (uint8_t i = 0; i < logCount(); i++) {
        const auto* e = &logEntries()[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
        gfx->setTextColor(e->color); gfx->setCursor(18, logY + 18 + (i * 11));
        gfx->printf("> %s", e->text);
    }

    // 4. Bongo Cat
    if (bongoCat_) {
        const bool active = lastMidiMs_ > 0 && nowMs - lastMidiMs_ < 400;
        bongoCat_->update(nowMs, active, me.noteEventsSeen);
        bongoCat_->draw(gfx, (240 - 128) / 2); 
    }

    // 5. Responsive Keyboard Bar (Bottom)
    drawKeyboardBar();

    drawToast(nowMs);
}

void BridgeUi::drawKeyboardBar()
{
    if (gfx == nullptr || engine_ == nullptr) return;
    const auto& me = engine_->state();
    
    const int16_t y = 232;
    const int16_t h = 8;
    const int16_t startX = 10;
    const int16_t keyW = 1; // Very thin for 128 keys in 220px... wait.
    // 128 keys in 220px is ~1.7px per key. Let's use 2px for a scrollable or 1px for fixed.
    // Let's use 1px for all 128 keys, centered.
    
    gfx->drawRect(startX - 1, y - 1, 130, h + 2, RGB565(40, 40, 50));
    for (int i = 0; i < 128; i++) {
        if (me.heldNotes[i]) {
            gfx->drawFastVLine(startX + i, y, h, RGB565_LIME);
        } else {
            // Background grid for octaves
            if (i % 12 == 0) {
                gfx->drawFastVLine(startX + i, y, h, RGB565(30, 30, 40));
            }
        }
    }
}

void BridgeUi::onOkTap() { cycleDisplayMode(); showToast(displayModeName(), millis()); }
void BridgeUi::onOkHold() { sendAllNotesOff(); showToast("PANIC", millis()); }
void BridgeUi::onUpTap() { engine_->setTranspose(engine_->transpose() + 1); char t[16]; snprintf(t, sizeof(t), "Trans %+d", engine_->transpose()); showToast(t, millis()); }
void BridgeUi::onDownTap() { engine_->setTranspose(engine_->transpose() - 1); char t[16]; snprintf(t, sizeof(t), "Trans %+d", engine_->transpose()); showToast(t, millis()); }
void BridgeUi::onMenuTap() { bridgeSettings.cycleMidiChannelFilter(); engine_->setChannelFilter(bridgeSettings.midiChannelFilter()); showToast("CHAN CYCLE", millis()); }
void BridgeUi::onMenuHold() { connectivityManager.startProvisioning(); showToast("WIFI SETUP", millis()); }

void BridgeUi::setBacklight(uint8_t level) { if (board_) board_->setBacklight(level); }
void BridgeUi::cycleDisplayMode() { applySavedDisplayMode((static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount)); }
void BridgeUi::showToast(const char* text, uint32_t nowMs) { strncpy(toastText_, text, sizeof(toastText_)-1); toastUntilMs_ = nowMs + 2000; }
void BridgeUi::drawToast(uint32_t nowMs) {
    if (nowMs < toastUntilMs_ && gfx) {
        gfx->fillRoundRect(70, 200, 100, 24, 4, RGB565(30, 30, 60));
        gfx->setTextColor(RGB565_WHITE); gfx->setTextSize(1);
        gfx->setCursor(80, 208); gfx->print(toastText_);
    }
}
void BridgeUi::sendAllNotesOff() { /* Panic logic */ }
void BridgeUi::drawOverlays(uint32_t nowMs) { (void)nowMs; }
bool BridgeUi::shouldDrawFullMetrics() const { return displayMode_ == DisplayMode::kFull; }
bool BridgeUi::shouldDrawStatusPanel() const { return displayMode_ == DisplayMode::kFull || displayMode_ == DisplayMode::kPerformance; }

const char* BridgeUi::displayModeName() const
{
    switch (displayMode_) {
        case DisplayMode::kFull: return "FULL";
        case DisplayMode::kPerformance: return "PERF";
        case DisplayMode::kMinimal: return "MINI";
        case DisplayMode::kStage: return "STAGE";
        default: return "UNKNOWN";
    }
}
