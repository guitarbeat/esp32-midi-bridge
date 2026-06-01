#include "BridgeUi.h"

#include "BLEConnection.h"
#include "Board.h"
#include "BridgeSystem.h"
#include "MidiCodec.h"
#include "USBConnection.h"

BridgeUi bridgeUi;

void BridgeUi::begin(Arduino_GFX* gfx_ptr)
{
    gfx = gfx_ptr;
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return;
    lastRefreshMs_ = nowMs;

    gfx->fillScreen(RGB565_BLACK);

    drawHeader(nowMs);
    drawStatusRow(nowMs);
    drawStatsRow();
    drawConsole(nowMs);
    drawKeyboardBar();
    drawToast(nowMs);
}

void BridgeUi::notifyStatus(const char* text, uint16_t color)
{
    if (text == nullptr) return;
    LogEntry& e = logs_[logHead_];
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    e.color = color;
    e.timestamp = millis();
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
}

void BridgeUi::cycleDisplayMode()
{
    displayMode_ = static_cast<DisplayMode>((static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount));
    bridgeSystem.saveDisplayMode(static_cast<uint8_t>(displayMode_));
    showToast(displayMode_ == DisplayMode::kFull ? "FULL" : "PERF", millis());
}

void BridgeUi::drawHeader(uint32_t nowMs)
{
    constexpr uint16_t kHeaderColor = RGB565(20, 20, 30);
    gfx->fillRect(0, 0, 240, 28, kHeaderColor);
    gfx->drawFastHLine(0, 28, 240, RGB565(60, 60, 80));

    gfx->setTextSize(1);

    const uint16_t pulseColor = (nowMs / 1000) % 2 == 0 ? RGB565_CYAN : RGB565(0, 40, 60);
    gfx->fillRect(8, 10, 4, 8, pulseColor);

    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(18, 10);
    gfx->print("PIANO BRIDGE");

    gfx->setTextColor(RGB565(120, 120, 140));
    gfx->setCursor(150, 10);
    gfx->printf("%lus", nowMs / 1000);

    if (board_) {
        const float v = board_->getBatteryVoltage();
        gfx->setCursor(200, 10);
        gfx->printf("%.1fV", v);
    }
}

void BridgeUi::drawStatusRow(uint32_t nowMs)
{
    (void)nowMs;

    auto drawChip = [&](int16_t x, int16_t y, const char* label, bool ok, const char* okText, const char* waitText, uint16_t okColor) {
        gfx->drawRoundRect(x, y, 108, 34, 4, RGB565(40, 40, 50));
        gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setTextSize(1);
        gfx->setCursor(x + 6, y + 4);
        gfx->print(label);
        gfx->setTextColor(ok ? okColor : RGB565_ORANGE);
        gfx->setCursor(x + 6, y + 17);
        gfx->print(ok ? okText : waitText);
    };

    const bool usbReady = diagnostics_.usb != nullptr && diagnostics_.usb->isConnected();
    const bool bleLinked = diagnostics_.ble != nullptr && diagnostics_.ble->isConnected();

    drawChip(8, 34, "USB HOST", usbReady, "READY", "WAIT", RGB565_LIME);
    drawChip(124, 34, "BLE OUT", bleLinked, "LINKED", "ADV...", RGB565_CYAN);
}

void BridgeUi::drawStatsRow()
{
    gfx->drawRoundRect(8, 72, 224, 28, 4, RGB565(40, 40, 50));
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(14, 78);
    gfx->printf("IN %lu  OUT %lu  SKIP %lu", diagnostics_.usbIn, diagnostics_.bleOut, diagnostics_.bleSkip);

    gfx->setTextColor(RGB565_GOLD);
    gfx->setCursor(14, 90);
    gfx->printf("TR %s  %s", bridgeSystem.transposeString(), bridgeSystem.channelString());

    if (diagnostics_.usb != nullptr && diagnostics_.usb->isConnected()) {
        const String& name = diagnostics_.usb->getDeviceName();
        if (name.length() > 0) {
            gfx->setTextColor(RGB565(100, 100, 120));
            gfx->setCursor(130, 90);
            gfx->printf("%.12s", name.c_str());
        }
    } else if (diagnostics_.usb != nullptr) {
        const String& err = diagnostics_.usb->getLastError();
        if (err.length() > 0) {
            gfx->setTextColor(RGB565_ORANGE);
            gfx->setCursor(100, 90);
            gfx->printf("%.18s", err.c_str());
        }
    }
}

void BridgeUi::drawConsole(uint32_t nowMs)
{
    (void)nowMs;
    const int16_t logY = 106;
    gfx->setTextColor(RGB565(100, 100, 120));
    gfx->setTextSize(1);
    gfx->setCursor(10, logY);
    gfx->print("MIDI LOG");
    gfx->fillRect(10, logY + 10, 220, 78, RGB565(10, 10, 15));
    gfx->drawRect(10, logY + 10, 220, 78, RGB565(30, 30, 40));

    for (uint8_t i = 0; i < logCount_; i++) {
        const auto* e = &logs_[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
        gfx->setTextColor(e->color);
        gfx->setCursor(16, logY + 16 + static_cast<int16_t>(i) * 14);
        gfx->printf("> %s", e->text);
    }
}

void BridgeUi::drawKeyboardBar()
{
    const auto& me = bridgeSystem.engine().state();
    const int16_t y = 232;
    const int16_t startX = 10;
    gfx->drawRect(startX - 1, y - 1, 130, 10, RGB565(40, 40, 50));
    for (int i = 0; i < 128; i++) {
        if (me.heldNotes[i]) {
            gfx->drawFastVLine(startX + i, y, 8, RGB565_LIME);
        } else if (i % 12 == 0) {
            gfx->drawFastVLine(startX + i, y, 8, RGB565(30, 30, 40));
        }
    }
}

void BridgeUi::showToast(const char* text, uint32_t nowMs)
{
    strncpy(toastText_, text, sizeof(toastText_) - 1);
    toastUntilMs_ = nowMs + 2000;
}

void BridgeUi::drawToast(uint32_t nowMs)
{
    if (nowMs < toastUntilMs_) {
        gfx->fillRoundRect(70, 196, 100, 24, 4, RGB565(30, 30, 60));
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(80, 204);
        gfx->print(toastText_);
    }
}
