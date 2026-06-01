#ifndef DIAG_DISPLAY_H
#define DIAG_DISPLAY_H

#include <Arduino_GFX_Library.h>

struct DiagStats {
    uint32_t usbIn = 0;
    uint32_t bleOutOk = 0;
    uint32_t bleOutSkip = 0;
    bool usbHostReady = false;
    bool bleConnected = false;
    char lastEvent[40];
    char lastResult[24];
};

class DiagDisplay {
public:
    void begin(Arduino_GFX* gfx);
    void setStats(const DiagStats& stats);
    void refresh(uint32_t nowMs);

private:
    void drawLog();

    Arduino_GFX* gfx_ = nullptr;
    DiagStats stats_{};
    uint32_t lastRefreshMs_ = 0;

    struct LogEntry {
        char text[36];
        uint16_t color;
    };
    static constexpr uint8_t kMaxLog = 5;
    LogEntry logs_[kMaxLog];
    uint8_t logHead_ = 0;
    uint8_t logCount_ = 0;

public:
    void pushLog(const char* text, uint16_t color);
};

extern DiagDisplay diagDisplay;

#endif
