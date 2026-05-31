#ifndef BRIDGE_UI_H
#define BRIDGE_UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class BLEConnection;
class Board;
class BongoCatDisplay;
class MidiBridge;
class MidiEngine;
class USBConnection;

class BridgeUi {
public:
    enum class DisplayMode : uint8_t {
        kFull = 0,
        kPerformance,
        kMinimal,
        kStage,
        kModeCount
    };

    void begin(Arduino_GFX* gfx);
    void applySavedDisplayMode(uint8_t modeIndex);
    void setBle(BLEConnection* ble);
    void setUsbMidi(USBConnection* usb);
    void setMidiBridge(MidiBridge* bridge);
    void setMidiEngine(MidiEngine* engine);
    void setBongoCat(BongoCatDisplay* bongoCat);
    void setBoard(Board* board);
    uint32_t backlightDimMs() const;

    void notifyMidiEvent(const uint8_t* data);
    void notifyRawMidiEvent(const uint8_t* data, size_t length);
    void notifyUsbStatus(bool connected);
    void notifyBleStatus(bool connected);

    void tick(uint32_t nowMs);
    void refresh(uint32_t nowMs, bool force = false);
    void drawOverlays(uint32_t nowMs);
    bool shouldDrawFullMetrics() const;
    bool shouldDrawStatusPanel() const;
    bool isBridgePaused() const { return bridgePaused_; }
    DisplayMode displayMode() const { return displayMode_; }

    const char* displayModeName() const;

    float batteryVoltage() const { return batteryVoltage_; }
    bool isUsbPowered() const { return isUsbPowered_; }

    struct MidiLogEntry {
        char text[32];
        uint16_t color;
        uint32_t timestamp;
    };

    void addLogEntry(const char* text, uint16_t color);
    const MidiLogEntry* logEntries() const { return logEntries_; }
    uint8_t logCount() const { return logCount_; }

    // Logical UI Actions (called by InputManager)
    void onOkTap();
    void onOkHold();
    void onUpTap();
    void onDownTap();
    void onMenuTap();
    void onMenuHold();

private:
    void cycleDisplayMode();
    void toggleBridgePaused();
    void showToast(const char* text, uint32_t nowMs);
    void drawToast(uint32_t nowMs);
    void drawKeyboardBar();
    void setBacklight(uint8_t level);
    void sendAllNotesOff();

    Arduino_GFX* gfx = nullptr;
    BLEConnection* ble = nullptr;
    USBConnection* usb_ = nullptr;
    MidiBridge* midiBridge_ = nullptr;
    MidiEngine* engine_ = nullptr;
    Board* board_ = nullptr;
    BongoCatDisplay* bongoCat_ = nullptr;

    uint32_t lastMidiMs_ = 0;
    char lastMidiText_[36] = "none";

    float batteryVoltage_ = 0.0f;
    bool isUsbPowered_ = true;
    bool usbConnected_ = false;
    bool bleConnected_ = false;

    static constexpr uint8_t kMaxLogEntries = 4;
    MidiLogEntry logEntries_[kMaxLogEntries];
    uint8_t logCount_ = 0;
    uint8_t logHead_ = 0;

    DisplayMode displayMode_ = DisplayMode::kFull;
    bool bridgePaused_ = false;
    uint8_t backlightLevel_ = 255;
    uint32_t lastActivityMs_ = 0;
    uint32_t lastRefreshMs_ = 0;

    char toastText_[40] = {0};
    uint32_t toastUntilMs_ = 0;

    static constexpr int16_t kSidebarW = 54;
};

extern BridgeUi bridgeUi;

#endif
