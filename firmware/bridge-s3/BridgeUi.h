#ifndef BRIDGE_UI_H
#define BRIDGE_UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class Board;
class BLEConnection;
class BridgeSystem;
class USBConnection;

struct BridgeUiRouteStats {
    uint32_t received = 0;
    uint32_t sent = 0;
    uint32_t skipped = 0;
    uint32_t failed = 0;
};

struct BridgeUiDiagnostics {
    const USBConnection* usb = nullptr;
    const BLEConnection* ble = nullptr;
    BridgeUiRouteStats usbStats;
    BridgeUiRouteStats bleStats;
    BridgeUiRouteStats rtpStats;
    BridgeUiRouteStats uartStats;
    bool rtpConnected = false;
    bool uartEnabled = false;
};

class BridgeUi {
public:
    enum class DisplayMode : uint8_t {
        kUnified = 0,
        kModeCount
    };

    void begin(Arduino_GFX* gfx);
    void setBoard(Board* board) { board_ = board; }
    void setDiagnostics(const BridgeUiDiagnostics& diag) { diagnostics_ = diag; }

    /** @brief Main refresh loop. */
    void refresh(uint32_t nowMs, bool force = false);

    /** @brief Called by system for high-priority display notifications. */
    void notifyStatus(const char* text, uint16_t color);

    DisplayMode displayMode() const { return displayMode_; }
    void setDisplayMode(DisplayMode mode) { (void)mode; displayMode_ = DisplayMode::kUnified; }
    void cycleDisplayMode();

private:
    void drawHeader(uint32_t nowMs);
    void drawStatusRow(uint32_t nowMs);
    void drawStatsRow();
    void drawUnifiedMode(uint32_t nowMs);
    void drawLivePanel(int16_t y);
    void drawVelocityBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t velocity);
    void drawMiniKeyboard(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t firstNote, uint8_t noteCount);
    uint8_t keyboardFirstNote() const { return keyboardFirstNote_; }
    void updateKeyboardViewport();
    void drawToast(uint32_t nowMs);
    void showToast(const char* text, uint32_t nowMs);

    Arduino_GFX* gfx = nullptr;
    Board* board_ = nullptr;
    BridgeUiDiagnostics diagnostics_{};
    
    DisplayMode displayMode_ = DisplayMode::kUnified;
    uint8_t keyboardFirstNote_ = 48;
    uint32_t lastRefreshMs_ = 0;
    uint32_t lastMidiMs_ = 0;

    struct LogEntry {
        char text[32];
        uint16_t color;
        uint32_t timestamp;
    };
    static constexpr uint8_t kMaxLogEntries = 5;
    LogEntry logs_[kMaxLogEntries];
    uint8_t logHead_ = 0;
    uint8_t logCount_ = 0;

    char toastText_[24];
    uint32_t toastUntilMs_ = 0;
};

extern BridgeUi bridgeUi;

#endif
