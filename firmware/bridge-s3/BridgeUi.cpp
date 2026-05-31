#include "BridgeUi.h"

#include "BLEConnection.h"
#include "BridgeSettings.h"
#include "MidiBridge.h"
#include "MidiCodec.h"
#include "MidiEngine.h"
#include "RTPMidiConfig.h"
#include "USBConnection.h"
#include "NetworkServices.h"
#include "animation/BongoCat.h"

BridgeUi bridgeUi;

void BridgeUi::begin(Arduino_GFX* gfx_ptr, int16_t backlight_pin)
{
    gfx = gfx_ptr;
    backlightPin = backlight_pin;
    lastActivityMs_ = millis();
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

void BridgeUi::setMidiEngine(MidiEngine* engine)
{
    engine_ = engine;
}

void BridgeUi::setBongoCat(BongoCatDisplay* bongoCat)
{
    bongoCat_ = bongoCat;
}

void BridgeUi::setBoard(Board* board)
{
    board_ = board;
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
    if (engine_ == nullptr) return;

    // Use a copy to allow engine to transform (e.g. transpose)
    uint8_t packet[4] = {data[0], data[1], data[2], data[3]};
    if (!engine_->processPacket(packet, 4)) return;

    const uint8_t status = packet[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;
    const uint8_t data1 = packet[2];
    const uint8_t data2 = packet[3];
    char noteBuffer[12] = {0};
    char logLine[32] = {0};

    if (messageType == 0x90 || messageType == 0x80) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer));
        snprintf(lastMidiText_, sizeof(lastMidiText_), "%s %s ch%u v%u", noteOn ? "On" : "Off", noteBuffer, channel, data2);
        snprintf(logLine, sizeof(logLine), "%-3s %s (v%u)", noteOn ? "ON" : "OFF", noteBuffer, data2);
        addLogEntry(logLine, noteOn ? RGB565_LIME : RGB565_DARKGRAY);
    } else if (messageType == 0xB0) {
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
    lastActivityMs_ = lastMidiMs_;
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

bool BridgeUi::shouldDrawFullMetrics() const
{
    return displayMode_ == DisplayMode::kFull;
}

bool BridgeUi::shouldDrawStatusPanel() const
{
    return displayMode_ == DisplayMode::kFull || displayMode_ == DisplayMode::kPerformance;
}

const char* BridgeUi::displayModeName() const
{
    switch (displayMode_) {
        case DisplayMode::kFull: return "FULL";
        case DisplayMode::kPerformance: return "PERF";
        case DisplayMode::kMinimal: return "MINI";
        case DisplayMode::kStage: return "STAGE";
        default: return "??";
    }
}

void BridgeUi::tick(uint32_t nowMs)
{
    if (engine_) engine_->tick(nowMs);
    handleBoardButtons(nowMs);
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
               String(engine_->transpose() > 0 ? "+" : "") + String(engine_->transpose()), 
               engine_->transpose() != 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    
    char chanBuf[8];
    if (engine_->channelFilter() == 0) strcpy(chanBuf, "ALL"); else sprintf(chanBuf, "CH%u", engine_->channelFilter());
    drawMetric(60, "FILTER", chanBuf, engine_->channelFilter() > 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    
    drawMetric(108, "MODE", displayModeName(), RGB565_CYAN);

    // Status Chips (Bottom of sidebar)
    if (board_) {
        const float v = board_->getBatteryVoltage();
        const bool isUsb = board_->isUsbPowered();
        const uint16_t bColor = v > 3.7f ? RGB565_LIME : (v > 3.4f ? RGB565_GOLD : RGB565_RED);
        gfx->drawRect(12, 210, 24, 12, RGB565(60, 60, 70));
        gfx->fillRect(36, 213, 2, 6, RGB565(60, 60, 70));
        float lvl = (v - 3.3f) / (4.2f - 3.3f);
        if (lvl > 1.0f) lvl = 1.0f; else if (lvl < 0.0f) lvl = 0.0f;
        gfx->fillRect(14, 212, (int)(lvl * 20), 8, isUsb ? RGB565_CYAN : bColor);
    }

    // Main Console Area
    const int16_t mainX = kSidebarW + 12;
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(mainX, 10);
    gfx->print("MIDI STATION");
    gfx->drawFastHLine(mainX, 28, 240 - mainX - 12, kBorderColor);

    if (shouldDrawStatusPanel()) {
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

        const int16_t logY = 78;
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setCursor(mainX, logY);
        gfx->print("ACTIVITY STREAM");
        
        gfx->fillRect(mainX, logY + 12, 162, 66, RGB565(10, 10, 15));
        gfx->drawRect(mainX, logY + 12, 162, 66, RGB565(25, 25, 35));
        
        const uint16_t pulseColor = (nowMs / 1000) % 2 == 0 ? RGB565(40, 40, 60) : RGB565(20, 20, 30);
        gfx->fillRect(mainX + 155, logY + 15, 4, 4, pulseColor);

        for (uint8_t i = 0; i < logCount(); i++) {
            const auto* e = &logEntries()[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
            const int16_t eY = logY + 17 + (i * 12);
            gfx->setTextColor(e->color);
            gfx->setCursor(mainX + 6, eY);
            gfx->printf("> %s", e->text);
        }

        if (midiBridge_) {
            const auto& c = midiBridge_->counters();
            gfx->setTextColor(RGB565(60, 60, 75));
            gfx->setCursor(mainX, 224);
            gfx->printf("IN: %u  OUT: %u  RPM: %u", c.usbPacketsSeen, c.blePacketsSent, me.notesPerMinute);
        }
    }

    const bool active = lastMidiMs_ > 0 && nowMs - lastMidiMs_ < 400;
    if (bongoCat_) {
        bongoCat_->update(nowMs, active, me.noteEventsSeen);
        bongoCat_->draw(gfx, 95); 
    }

    drawOverlays(nowMs);
}

void BridgeUi::drawOverlays(uint32_t nowMs)
{
    // Implementation of overlays...
}

// Button handling...
void BridgeUi::handleBoardButtons(uint32_t nowMs)
{
    // Simplified for refactor
}
