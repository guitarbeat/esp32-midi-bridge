#ifndef BRIDGE_UI_H
#define BRIDGE_UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class BLEConnection;
class BongoCatDisplay;
class MidiBridge;
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

    void begin(Arduino_GFX* gfx, int16_t backlightPin);
    void applySavedDisplayMode(uint8_t modeIndex);
    void setBle(BLEConnection* ble);
    void setUsbMidi(USBConnection* usb);
    void setMidiBridge(MidiBridge* bridge);
    void setBongoCat(BongoCatDisplay* bongoCat);
    uint32_t backlightDimMs() const;

    void onNoteEvent(bool noteOn, uint8_t note, uint8_t velocity);
    void onControlChange(uint8_t cc, uint8_t value);
    void touchActivity(uint32_t nowMs);

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

    uint8_t heldNoteCount() const;
    uint16_t notesPerMinute() const;
    uint8_t lastVelocity() const { return lastVelocity_; }
    bool sustainDown() const { return sustainDown_; }

    const char* displayModeName() const;
    const char* lastNoteLabel() const { return lastNoteLabel_; }

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

private:
    Arduino_GFX* gfx = nullptr;
    BLEConnection* ble = nullptr;
    USBConnection* usb_ = nullptr;
    MidiBridge* midiBridge_ = nullptr;
    BongoCatDisplay* bongoCat_ = nullptr;
    int16_t backlightPin = -1;

    bool heldNotes[128] = {false};
    uint8_t heldCount_ = 0;
    uint8_t lastVelocity_ = 0;
    uint8_t lastNote_ = 0;
    bool sustainDown_ = false;
    char lastNoteLabel_[12] = "--";
    uint32_t lastMidiMs_ = 0;
    char lastMidiText_[36] = "none";
    uint32_t noteEventsSeen_ = 0;

    uint32_t noteOnTimes[32] = {0};
    uint8_t noteOnHead_ = 0;
    uint16_t notesPerMinute_ = 0;
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

    bool pauseToggleFired_ = false;
    bool menuWifiSetupFired_ = false;

    char toastText_[40] = {0};
    uint32_t toastUntilMs_ = 0;

    struct BoardButton {
        int8_t pin = -1;
        bool down = false;
        uint32_t downMs = 0;
        bool actionFired = false;
    };

    BoardButton okButton_;
    BoardButton upButton_;
    BoardButton downButton_;
    BoardButton menuButton_;

    void cycleDisplayMode();
    void toggleBridgePaused();
    void sendAllNotesOff();
    void showToast(const char* text, uint32_t nowMs);
    void updateNotesPerMinute(uint32_t nowMs);
    void refreshLastNoteLabel();
    void setBacklight(uint8_t level);
    void handleBoardButtons(uint32_t nowMs);
    void initBoardButtons();
    void drawToast(uint32_t nowMs);
    void drawMiniKeyboard();
    void drawVelocityBar();
    void drawStatusChips(uint32_t nowMs);
};

extern BridgeUi bridgeUi;

#endif
