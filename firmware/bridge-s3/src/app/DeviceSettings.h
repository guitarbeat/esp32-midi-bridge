#ifndef BRIDGE_SETTINGS_H
#define BRIDGE_SETTINGS_H

#include <Arduino.h>

class DeviceSettings {
public:
    static constexpr uint8_t kBleNameMax = 31;
    static constexpr int8_t kTransposeMin = -12;
    static constexpr int8_t kTransposeMax = 12;

    bool begin(const char* defaultBleName);

    const char* bleDeviceName() const { return bleName_; }
    uint32_t backlightDimMs() const { return backlightDimMs_; }
    int8_t transposeSemitones() const { return transpose_; }
    uint8_t midiChannelFilter() const { return midiChannel_; }  // 0 = all channels
    bool uartEnabled() const { return uartEnabled_; }
    uint32_t uartBaudRate() const { return uartBaudRate_; }

    void stepTranspose(int8_t delta);
    void cycleTranspose();
    void cycleMidiChannelFilter();
    void cycleBacklightDim();
    void toggleUart();
    void cycleUartBaudRate();

    void printSummary() const;

private:
    char bleName_[kBleNameMax + 1] = {0};
    uint32_t backlightDimMs_ = 90000;
    int8_t transpose_ = 0;
    uint8_t midiChannel_ = 0;
    bool uartEnabled_ = false;
    uint32_t uartBaudRate_ = 31250;

    void load();
    void saveAll();
};

extern DeviceSettings deviceSettings;

#endif
