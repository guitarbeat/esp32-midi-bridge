#include "DiagDisplay.h"

DiagDisplay diagDisplay;

void DiagDisplay::begin(Arduino_GFX* gfx) {
    gfx_ = gfx;
}

void DiagDisplay::setStats(const DiagStats& stats) {
    stats_ = stats;
}

void DiagDisplay::pushLog(const char* text, uint16_t color) {
    if (text == nullptr) return;
    LogEntry& e = logs_[logHead_];
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    e.color = color;
    logHead_ = (logHead_ + 1) % kMaxLog;
    if (logCount_ < kMaxLog) logCount_++;
}

void DiagDisplay::drawLog() {
    const int16_t logY = 118;
    gfx_->setTextColor(RGB565(100, 100, 120));
    gfx_->setTextSize(1);
    gfx_->setCursor(8, logY);
    gfx_->print("MIDI LOG");
    gfx_->fillRect(8, logY + 10, 224, 88, RGB565(10, 10, 15));
    gfx_->drawRect(8, logY + 10, 224, 88, RGB565(30, 30, 40));

    for (uint8_t i = 0; i < logCount_; i++) {
        const LogEntry* e = &logs_[(logHead_ - logCount_ + i + kMaxLog) % kMaxLog];
        gfx_->setTextColor(e->color);
        gfx_->setCursor(14, logY + 16 + static_cast<int16_t>(i) * 16);
        gfx_->printf("> %s", e->text);
    }
}

void DiagDisplay::refresh(uint32_t nowMs) {
    if (gfx_ == nullptr) return;
    if (nowMs - lastRefreshMs_ < 80) return;
    lastRefreshMs_ = nowMs;

    gfx_->fillScreen(RGB565_BLACK);

    gfx_->fillRect(0, 0, 240, 28, RGB565(20, 20, 30));
    gfx_->drawFastHLine(0, 28, 240, RGB565(60, 60, 80));
    gfx_->setTextSize(1);
    gfx_->setTextColor(RGB565_CYAN);
    gfx_->setCursor(8, 9);
    gfx_->print("HOST MIDI  USB->BLE");
    gfx_->setTextColor(RGB565(120, 120, 140));
    gfx_->setCursor(170, 9);
    gfx_->printf("%lus", nowMs / 1000);

    auto statusChip = [&](int16_t x, int16_t y, const char* label, bool ok, const char* okText, const char* waitText) {
        gfx_->drawRoundRect(x, y, 108, 36, 4, RGB565(40, 40, 50));
        gfx_->setTextColor(RGB565(100, 100, 120));
        gfx_->setCursor(x + 8, y + 4);
        gfx_->print(label);
        gfx_->setTextColor(ok ? RGB565_LIME : RGB565_ORANGE);
        gfx_->setCursor(x + 8, y + 18);
        gfx_->print(ok ? okText : waitText);
    };

    statusChip(8, 36, "USB HOST", stats_.usbHostReady, "READY", "INIT...");
    statusChip(124, 36, "BLE OUT", stats_.bleConnected, "LINKED", "ADV...");

    gfx_->drawRoundRect(8, 78, 224, 32, 4, RGB565(40, 40, 50));
    gfx_->setTextColor(RGB565_LIGHTGRAY);
    gfx_->setCursor(14, 84);
    gfx_->printf("IN %lu  OUT %lu  SKIP %lu", stats_.usbIn, stats_.bleOutOk, stats_.bleOutSkip);
    gfx_->setCursor(14, 96);
    gfx_->setTextColor(RGB565_GOLD);
    gfx_->printf("Last: %s", stats_.lastEvent[0] ? stats_.lastEvent : "(none)");
    gfx_->setTextColor(RGB565(160, 160, 180));
    gfx_->setCursor(14, 108);
    gfx_->printf("-> %s", stats_.lastResult[0] ? stats_.lastResult : "-");

    drawLog();
}
