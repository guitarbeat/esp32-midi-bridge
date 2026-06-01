#include "BridgeUi.h"

#include "BLEConnection.h"
#include "Board.h"
#include "BridgeSystem.h"
#include "MidiCodec.h"
#include "USBConnection.h"

BridgeUi bridgeUi;

namespace {

constexpr uint8_t kKeyboardVisibleNotes = 24;
constexpr uint8_t kDefaultFirstNote = 48;  // C3
constexpr int16_t kFullKeyboardY = 178;
constexpr uint16_t kPanelBorder = RGB565(40, 40, 50);
constexpr uint16_t kPanelFill = RGB565(10, 10, 15);
constexpr uint16_t kPanelAccent = RGB565(70, 70, 90);
constexpr uint16_t kMutedText = RGB565(100, 100, 120);

bool isBlackKey(uint8_t note)
{
    switch (note % 12) {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

const char* displayModeName(BridgeUi::DisplayMode mode)
{
    switch (mode) {
        case BridgeUi::DisplayMode::kFull: return "FULL";
        case BridgeUi::DisplayMode::kPerformance: return "PERF";
        case BridgeUi::DisplayMode::kMinimal: return "MIN";
        case BridgeUi::DisplayMode::kStage: return "STAGE";
        default: return "FULL";
    }
}

uint8_t clampKeyboardFirstNote(int firstNote)
{
    if (firstNote < 0) {
        return 0;
    }
    constexpr int kMaxFirst = 128 - kKeyboardVisibleNotes;
    if (firstNote > kMaxFirst) {
        return kMaxFirst;
    }
    return static_cast<uint8_t>(firstNote);
}

}  // namespace

void BridgeUi::begin(Arduino_GFX* gfx_ptr)
{
    gfx = gfx_ptr;
    keyboardFirstNote_ = kDefaultFirstNote;
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return;
    lastRefreshMs_ = nowMs;

    gfx->fillScreen(RGB565_BLACK);
    updateKeyboardViewport();

    drawHeader(nowMs);
    switch (displayMode_) {
        case DisplayMode::kPerformance:
            drawPerformanceMode();
            break;
        case DisplayMode::kMinimal:
            drawMinimalMode();
            break;
        case DisplayMode::kStage:
            drawStageMode();
            break;
        case DisplayMode::kFull:
        default:
            drawFullMode(nowMs);
            break;
    }
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
    showToast(displayModeName(displayMode_), millis());
}

void BridgeUi::drawFullMode(uint32_t nowMs)
{
    (void)nowMs;
    drawTransportRail(36);
    drawLivePanel(62);
    drawVelocityBar(18, 128, 204, 18, bridgeSystem.engine().state().lastVelocity);
    drawMetricStrip(154);
    drawMiniKeyboard(8, kFullKeyboardY, 224, 54, keyboardFirstNote(), kKeyboardVisibleNotes);
}

void BridgeUi::drawPerformanceMode()
{
    drawStatusRow(millis());
    drawStatsRow();
    drawConsole(millis(), 3);
    drawPerformanceSummary(160);
    drawMiniKeyboard(8, kFullKeyboardY, 224, 54, keyboardFirstNote(), kKeyboardVisibleNotes);
}

void BridgeUi::drawMinimalMode()
{
    const auto& me = bridgeSystem.engine().state();
    const bool usbReady = diagnostics_.usb != nullptr && diagnostics_.usb->isConnected();
    const bool bleLinked = diagnostics_.ble != nullptr && diagnostics_.ble->isConnected();
    const bool bleReady = diagnostics_.ble != nullptr && diagnostics_.ble->isSubscribed();

    gfx->setTextSize(1);
    gfx->setTextColor(usbReady ? RGB565_LIME : RGB565_ORANGE);
    gfx->setCursor(20, 48);
    gfx->printf("USB %s", usbReady ? "READY" : "WAIT");
    gfx->setTextColor(bleReady ? RGB565_CYAN : (bleLinked ? RGB565_GOLD : RGB565_ORANGE));
    gfx->setCursor(132, 48);
    gfx->printf("BLE %s", bleReady ? "READY" : (bleLinked ? "LINK" : "ADV"));

    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(4);
    gfx->setCursor(70, 86);
    gfx->printf("%s", me.lastNoteLabel);

    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(50, 136);
    gfx->printf("VEL %u   HELD %u", me.lastVelocity, me.heldCount);

    drawMiniKeyboard(18, 168, 204, 56, keyboardFirstNote(), kKeyboardVisibleNotes);
}

void BridgeUi::drawStageMode()
{
    const auto& me = bridgeSystem.engine().state();

    gfx->setTextSize(5);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(48, 48);
    gfx->printf("%s", me.lastNoteLabel);

    gfx->setTextSize(1);
    gfx->setTextColor(me.sustainDown ? RGB565_GOLD : kMutedText);
    gfx->setCursor(78, 102);
    gfx->printf("VEL %u  %s", me.lastVelocity, me.sustainDown ? "SUSTAIN" : "DRY");

    drawMiniKeyboard(6, 124, 228, 106, keyboardFirstNote(), kKeyboardVisibleNotes);
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

    auto drawChip = [&](int16_t x, int16_t y, int16_t w, const char* label, const char* stateText, uint16_t stateColor) {
        gfx->drawRoundRect(x, y, w, 34, 4, kPanelBorder);
        gfx->setTextColor(kMutedText);
        gfx->setTextSize(1);
        gfx->setCursor(x + 4, y + 4);
        gfx->print(label);
        gfx->setTextColor(stateColor);
        gfx->setCursor(x + 4, y + 17);
        gfx->print(stateText);
    };

    const bool usbReady = diagnostics_.usb != nullptr && diagnostics_.usb->isConnected();
    const bool usbOutReady = diagnostics_.usb != nullptr && diagnostics_.usb->canSend();
    const bool bleLinked = diagnostics_.ble != nullptr && diagnostics_.ble->isConnected();
    const bool bleReady = diagnostics_.ble != nullptr && diagnostics_.ble->isSubscribed();
    const uint32_t usbRaw = diagnostics_.usb != nullptr ? diagnostics_.usb->getRawUsbPacketsSeen() : 0;
    const uint32_t usbDecoded = diagnostics_.usb != nullptr ? diagnostics_.usb->getDecodedMidiPacketsSeen() : 0;

    const char* usbInText = "WAIT";
    uint16_t usbInColor = RGB565_ORANGE;
    if (usbReady && usbDecoded > 0) {
        usbInText = "MIDI";
        usbInColor = RGB565_LIME;
    } else if (usbReady && usbRaw > 0) {
        usbInText = "RAW";
        usbInColor = RGB565_GOLD;
    } else if (usbReady) {
        usbInText = "NO MIDI";
        usbInColor = RGB565_GOLD;
    } else if (diagnostics_.usb != nullptr && diagnostics_.usb->hasSeenDevice()) {
        usbInText = "NO MIDI";
        usbInColor = RGB565_ORANGE;
    }

    drawChip(4, 34, 56, "USB IN", usbInText, usbInColor);
    drawChip(62, 34, 56, "USB OUT", usbOutReady ? "READY" : (usbReady ? "N/A" : "WAIT"), usbOutReady ? RGB565_LIME : RGB565_ORANGE);
    drawChip(120, 34, 56, "BLE", bleReady ? "READY" : (bleLinked ? "OPEN" : "ADV"), bleReady ? RGB565_CYAN : RGB565_ORANGE);
    drawChip(178, 34, 58, "RTP", diagnostics_.rtpConnected ? "LINK" : "WAIT", diagnostics_.rtpConnected ? RGB565_MAGENTA : RGB565_ORANGE);
}

void BridgeUi::drawStatsRow()
{
    gfx->drawRoundRect(8, 72, 224, 28, 4, kPanelBorder);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(14, 78);
    const uint32_t usbRaw = diagnostics_.usb != nullptr ? diagnostics_.usb->getRawUsbPacketsSeen() : 0;
    const uint32_t usbDrops = diagnostics_.usb != nullptr ? diagnostics_.usb->getDecodeDropCount() : 0;
    if (diagnostics_.usbStats.received == 0 && (usbRaw > 0 || usbDrops > 0)) {
        gfx->printf("USB RAW %lu  MIDI %lu  DROP %lu",
                    usbRaw,
                    diagnostics_.usbStats.received,
                    usbDrops);
    } else {
        gfx->printf("RX U%lu B%lu R%lu S%lu",
                    diagnostics_.usbStats.received,
                    diagnostics_.bleStats.received,
                    diagnostics_.rtpStats.received,
                    diagnostics_.uartStats.received);
    }

    gfx->setTextColor(RGB565_GOLD);
    gfx->setCursor(14, 90);
    gfx->printf("TX U%lu B%lu R%lu S%lu",
                diagnostics_.usbStats.sent,
                diagnostics_.bleStats.sent,
                diagnostics_.rtpStats.sent,
                diagnostics_.uartStats.sent);

    if (diagnostics_.usb != nullptr && !diagnostics_.usb->isConnected()) {
        const String& err = diagnostics_.usb->getLastError();
        if (err.length() > 0) {
            gfx->setTextColor(RGB565_ORANGE);
            gfx->setCursor(100, 90);
            gfx->printf("%.18s", err.c_str());
        }
    }
}

void BridgeUi::drawTransportRail(int16_t y)
{
    const bool usbReady = diagnostics_.usb != nullptr && diagnostics_.usb->isConnected();
    const bool bleLinked = diagnostics_.ble != nullptr && diagnostics_.ble->isConnected();
    const bool bleReady = diagnostics_.ble != nullptr && diagnostics_.ble->isSubscribed();
    const uint32_t usbRaw = diagnostics_.usb != nullptr ? diagnostics_.usb->getRawUsbPacketsSeen() : 0;
    const uint32_t usbDecoded = diagnostics_.usb != nullptr ? diagnostics_.usb->getDecodedMidiPacketsSeen() : 0;

    const char* usbState = "WAIT";
    uint16_t usbColor = RGB565_ORANGE;
    if (usbReady && usbDecoded > 0) {
        usbState = "MIDI";
        usbColor = RGB565_LIME;
    } else if (usbReady && usbRaw > 0) {
        usbState = "RAW";
        usbColor = RGB565_GOLD;
    } else if (usbReady || (diagnostics_.usb != nullptr && diagnostics_.usb->hasSeenDevice())) {
        usbState = "NOMID";
        usbColor = RGB565_GOLD;
    }

    struct RailItem {
        const char* label;
        const char* value;
        uint16_t color;
    };

    const RailItem items[] = {
        {"USB", usbState, usbColor},
        {"BLE", bleReady ? "OK" : (bleLinked ? "APP" : "ADV"), bleReady ? RGB565_CYAN : (bleLinked ? RGB565_GOLD : RGB565_ORANGE)},
        {"RTP", diagnostics_.rtpConnected ? "OK" : "--", diagnostics_.rtpConnected ? RGB565_MAGENTA : kMutedText},
        {"CH", bridgeSystem.channelString(), RGB565_LIGHTGRAY}
    };

    constexpr int16_t itemW = 54;
    for (uint8_t i = 0; i < 4; i++) {
        const int16_t x = 8 + static_cast<int16_t>(i) * 56;
        gfx->drawRect(x, y, itemW, 20, RGB565(28, 28, 40));
        gfx->setTextSize(1);
        gfx->setTextColor(kMutedText);
        gfx->setCursor(x + 4, y + 7);
        gfx->print(items[i].label);
        drawShortText(items[i].value, x + 26, y + 7, itemW - 30, items[i].color, 1);
    }
}

void BridgeUi::drawLivePanel(int16_t y)
{
    const auto& me = bridgeSystem.engine().state();
    gfx->drawRoundRect(8, y, 224, 58, 4, kPanelBorder);
    gfx->fillRect(10, y + 2, 220, 54, RGB565(5, 5, 9));

    gfx->setTextSize(1);
    gfx->setTextColor(kMutedText);
    gfx->setCursor(18, y + 8);
    gfx->print("LAST NOTE");

    const uint8_t noteLen = strlen(me.lastNoteLabel);
    const int16_t noteW = static_cast<int16_t>(noteLen) * 24;  // 6 px glyph * text size 4
    const int16_t noteX = 120 - (noteW / 2);
    gfx->setTextSize(4);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(noteX < 16 ? 16 : noteX, y + 20);
    gfx->print(me.lastNoteLabel);
}

void BridgeUi::drawVelocityBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t velocity)
{
    const int16_t labelW = 28;
    const int16_t valueW = 24;
    const int16_t barX = x + labelW;
    const int16_t barW = w - labelW - valueW - 6;
    const int16_t fillW = static_cast<int16_t>((static_cast<uint32_t>(barW - 4) * velocity) / 127);

    gfx->setTextSize(1);
    gfx->setTextColor(kMutedText);
    gfx->setCursor(x, y + 5);
    gfx->print("VEL");

    gfx->drawRoundRect(barX, y, barW, h, 4, kPanelAccent);
    gfx->fillRoundRect(barX + 2, y + 2, barW - 4, h - 4, 3, RGB565(12, 12, 18));
    if (velocity > 0 && fillW > 0) {
        gfx->fillRoundRect(barX + 2, y + 2, fillW, h - 4, 3, RGB565_GOLD);
    }

    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(barX + barW + 6, y + 5);
    gfx->printf("%3u", velocity);
}

void BridgeUi::drawMetricStrip(int16_t y)
{
    const auto& me = bridgeSystem.engine().state();
    const uint32_t usbRaw = diagnostics_.usb != nullptr ? diagnostics_.usb->getRawUsbPacketsSeen() : 0;
    const uint32_t usbDrops = diagnostics_.usb != nullptr ? diagnostics_.usb->getDecodeDropCount() : 0;
    gfx->drawFastHLine(12, y, 216, RGB565(28, 28, 40));

    gfx->setTextSize(1);
    if (diagnostics_.usbStats.received == 0 &&
        diagnostics_.usb != nullptr &&
        (diagnostics_.usb->isConnected() || usbRaw > 0 || usbDrops > 0)) {
        gfx->setTextColor(RGB565_LIGHTGRAY);
        gfx->setCursor(16, y + 12);
        gfx->printf("USB RAW %lu", usbRaw);
        gfx->setTextColor(usbDrops > 0 ? RGB565_GOLD : kMutedText);
        gfx->setCursor(116, y + 12);
        gfx->printf("DROP %lu", usbDrops);
        return;
    }

    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(16, y + 12);
    gfx->printf("HELD %u", me.heldCount);

    gfx->setTextColor(me.sustainDown ? RGB565_GOLD : kMutedText);
    gfx->setCursor(86, y + 12);
    gfx->printf("SUS %s", me.sustainDown ? "ON" : "OFF");

    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(162, y + 12);
    gfx->printf("NPM %u", me.notesPerMinute);
}

void BridgeUi::drawShortText(const char* text, int16_t x, int16_t y, int16_t maxW, uint16_t color, uint8_t textSize)
{
    if (text == nullptr || maxW <= 0 || textSize == 0) {
        return;
    }

    char clipped[12];
    const uint8_t maxChars = maxW / (6 * textSize);
    if (maxChars == 0) {
        return;
    }
    uint8_t count = maxChars < sizeof(clipped) - 1 ? maxChars : sizeof(clipped) - 1;
    strncpy(clipped, text, count);
    clipped[count] = '\0';

    gfx->setTextSize(textSize);
    gfx->setTextColor(color);
    gfx->setCursor(x, y);
    gfx->print(clipped);
}

void BridgeUi::drawConsole(uint32_t nowMs, uint8_t maxLines)
{
    (void)nowMs;
    const int16_t logY = 106;
    gfx->setTextColor(kMutedText);
    gfx->setTextSize(1);
    gfx->setCursor(10, logY);
    gfx->print("MIDI LOG");
    gfx->fillRect(10, logY + 10, 220, 42, kPanelFill);
    gfx->drawRect(10, logY + 10, 220, 42, RGB565(30, 30, 40));

    const uint8_t lines = logCount_ < maxLines ? logCount_ : maxLines;
    for (uint8_t i = 0; i < lines; i++) {
        const auto* e = &logs_[(logHead_ - lines + i + kMaxLogEntries) % kMaxLogEntries];
        gfx->setTextColor(e->color);
        gfx->setCursor(16, logY + 16 + static_cast<int16_t>(i) * 14);
        gfx->printf("> %s", e->text);
    }
}

void BridgeUi::drawPerformanceSummary(int16_t y)
{
    const auto& me = bridgeSystem.engine().state();
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(10, y);
    gfx->printf("Last %s  Vel %u  Held %u  Sus %s",
                me.lastNoteLabel,
                me.lastVelocity,
                me.heldCount,
                me.sustainDown ? "ON" : "OFF");
}

void BridgeUi::drawMiniKeyboard(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t firstNote, uint8_t noteCount)
{
    if (noteCount > kKeyboardVisibleNotes) {
        noteCount = kKeyboardVisibleNotes;
    }
    const auto& me = bridgeSystem.engine().state();
    const int16_t labelH = 10;
    const int16_t keyH = h - labelH;
    const int16_t blackH = (keyH * 3) / 5;

    gfx->fillRect(x, y, w, h, RGB565_BLACK);
    gfx->drawRect(x, y, w, keyH, kPanelBorder);

    uint8_t whiteCount = 0;
    for (uint8_t i = 0; i < noteCount; i++) {
        if (!isBlackKey(firstNote + i)) {
            whiteCount++;
        }
    }
    if (whiteCount == 0) return;

    const int16_t whiteW = (w / whiteCount) > 1 ? (w / whiteCount) : 1;
    uint8_t whiteIndex = 0;
    int16_t noteLeft[24] = {0};
    int16_t noteRight[24] = {0};

    for (uint8_t i = 0; i < noteCount; i++) {
        const uint8_t note = firstNote + i;
        if (isBlackKey(note)) continue;

        const int16_t keyX = x + (whiteIndex * w) / whiteCount;
        const int16_t nextX = x + ((whiteIndex + 1) * w) / whiteCount;
        const bool held = me.heldNotes[note];
        const int16_t fillW = (nextX - keyX - 1) > 1 ? (nextX - keyX - 1) : 1;
        const int16_t outlineW = (nextX - keyX) > 1 ? (nextX - keyX) : 1;
        gfx->fillRect(keyX + 1, y + 1, fillW, keyH - 2, held ? RGB565_LIME : RGB565(220, 220, 210));
        gfx->drawRect(keyX, y, outlineW, keyH, RGB565(70, 70, 75));

        noteLeft[i] = keyX;
        noteRight[i] = nextX;

        if (note % 12 == 0) {
            gfx->setTextSize(1);
            gfx->setTextColor(kMutedText);
            gfx->setCursor(keyX + 1, y + keyH + 2);
            gfx->printf("C%d", (note / 12) - 1);
        }

        whiteIndex++;
    }

    for (uint8_t i = 0; i < noteCount; i++) {
        const uint8_t note = firstNote + i;
        if (!isBlackKey(note)) continue;

        int8_t leftWhite = static_cast<int8_t>(i) - 1;
        while (leftWhite >= 0 && isBlackKey(firstNote + leftWhite)) {
            leftWhite--;
        }
        if (leftWhite < 0) continue;

        const int16_t anchorX = noteRight[leftWhite];
        const int16_t blackW = (whiteW / 2) > 4 ? (whiteW / 2) : 4;
        const int16_t keyX = anchorX - (blackW / 2);
        const bool held = me.heldNotes[note];
        gfx->fillRect(keyX, y + 1, blackW, blackH, held ? RGB565_CYAN : RGB565(18, 18, 24));
        gfx->drawRect(keyX, y + 1, blackW, blackH, RGB565(80, 80, 95));
    }
}

void BridgeUi::updateKeyboardViewport()
{
    const auto& me = bridgeSystem.engine().state();
    if (me.noteEventsSeen == 0) {
        keyboardFirstNote_ = kDefaultFirstNote;
        return;
    }

    const uint8_t lastNote = me.lastNote;
    if (lastNote >= keyboardFirstNote_ && lastNote < keyboardFirstNote_ + kKeyboardVisibleNotes) {
        return;
    }

    int first = (static_cast<int>(lastNote) / 12 - 1) * 12;
    if (first < 0) {
        first = 0;
    }
    keyboardFirstNote_ = clampKeyboardFirstNote(first);
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
