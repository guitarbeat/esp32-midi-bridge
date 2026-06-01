#include <iostream>
#include <cassert>
#include <cstring>
#include "../firmware/bridge-s3/MidiCodec.h"

void testLengthFromStatus() {
    assert(MidiCodec::lengthFromStatus(0x90) == 3); // Note On
    assert(MidiCodec::lengthFromStatus(0x80) == 3); // Note Off
    assert(MidiCodec::lengthFromStatus(0xC0) == 2); // Program Change
    assert(MidiCodec::lengthFromStatus(0xF8) == 1); // Timing Clock
    std::cout << "testLengthFromStatus passed!" << std::endl;
}

void testNoteName() {
    char buffer[12];
    MidiCodec::noteName(60, buffer, sizeof(buffer)); // Middle C
    assert(std::string(buffer) == "C4");
    
    MidiCodec::noteName(21, buffer, sizeof(buffer)); // A0
    assert(std::string(buffer) == "A0");
    
    MidiCodec::noteName(108, buffer, sizeof(buffer)); // C8
    assert(std::string(buffer) == "C8");
    std::cout << "testNoteName passed!" << std::endl;
}

void testParser() {
    static uint8_t lastStatus = 0;
    static uint8_t lastData[3] = {0};
    static size_t lastLen = 0;
    static size_t lastSysexPos = 0;

    auto callback = [](uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg) {
        lastStatus = status;
        lastLen = length;
        lastSysexPos = sysexPos;
        if (length > 0 && data) std::memcpy(lastData, data, length);
    };

    MidiCodec::Parser parser(callback);

    // 1. Simple Note On
    parser.parse(0x90);
    parser.parse(60);
    parser.parse(100);
    assert(lastStatus == 0x90);
    assert(lastLen == 2);
    assert(lastData[0] == 60);
    assert(lastData[1] == 100);

    // 2. Running Status
    lastStatus = 0;
    parser.parse(61);
    parser.parse(101);
    assert(lastStatus == 0x90);
    assert(lastData[0] == 61);
    assert(lastData[1] == 101);

    // 3. Real-time message during Note On (interleaved)
    lastStatus = 0;
    parser.parse(62);
    parser.parse(0xF8); // Timing Clock
    assert(lastStatus == 0xF8);
    assert(lastLen == 0);
    parser.parse(102);
    assert(lastStatus == 0x90);
    assert(lastData[0] == 62);
    assert(lastData[1] == 102);

    // 4. SysEx with Position Tracking
    lastStatus = 0;
    parser.parse(0xF0); // Start
    assert(lastStatus == 0xF0);
    assert(lastSysexPos == 0);
    
    parser.parse(0x01); // Data 1
    assert(lastStatus == 0xF0);
    assert(lastLen == 1);
    assert(lastData[0] == 0x01);
    assert(lastSysexPos == 0); // Pos 0 for first data
    
    parser.parse(0x02); // Data 2
    assert(lastSysexPos == 1);
    
    parser.parse(0xF7); // End
    assert(lastStatus == 0xF7);
    assert(lastSysexPos == 2); // F7 is at pos 2

    std::cout << "testParser passed!" << std::endl;
}

void testDecodeUsbEventToRaw() {
    uint8_t runningStatus[16] = {0};
    uint8_t raw[4] = {0};
    size_t rawLen = 0;

    const uint8_t noteOn[] = {0x09, 0x90, 60, 100};
    assert(MidiCodec::decodeUsbEventToRaw(noteOn, runningStatus, raw, &rawLen));
    assert(rawLen == 3);
    assert(raw[0] == 0x90 && raw[1] == 60 && raw[2] == 100);

    const uint8_t noteOnRs[] = {0x09, 0x00, 61, 90};
    assert(MidiCodec::decodeUsbEventToRaw(noteOnRs, runningStatus, raw, &rawLen));
    assert(rawLen == 3);
    assert(raw[0] == 0x90 && raw[1] == 61 && raw[2] == 90);

    const uint8_t programChange[] = {0x0C, 0xC0, 5, 0};
    assert(MidiCodec::decodeUsbEventToRaw(programChange, runningStatus, raw, &rawLen));
    assert(rawLen == 2);
    assert(raw[0] == 0xC0 && raw[1] == 5);

    std::cout << "testDecodeUsbEventToRaw passed!" << std::endl;
}

void testFormatLogLine() {
    char line[32] = {0};

    const uint8_t programChange[] = {0xC2, 12};
    assert(MidiCodec::formatLogLine(programChange, sizeof(programChange), line, sizeof(line)));
    assert(std::string(line) == "PC ch3 #12");

    const uint8_t channelPressure[] = {0xD0, 64};
    assert(MidiCodec::formatLogLine(channelPressure, sizeof(channelPressure), line, sizeof(line)));
    assert(std::string(line) == "AT ch1 v64");

    const uint8_t pitchBendCenter[] = {0xE0, 0x00, 0x40};
    assert(MidiCodec::formatLogLine(pitchBendCenter, sizeof(pitchBendCenter), line, sizeof(line)));
    assert(std::string(line) == "BEND ch1 0");

    const uint8_t pitchBendDown[] = {0xE4, 0x00, 0x00};
    assert(MidiCodec::formatLogLine(pitchBendDown, sizeof(pitchBendDown), line, sizeof(line)));
    assert(std::string(line) == "BEND ch5 -8192");

    std::cout << "testFormatLogLine passed!" << std::endl;
}

int main() {
    testLengthFromStatus();
    testNoteName();
    testParser();
    testDecodeUsbEventToRaw();
    testFormatLogLine();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
