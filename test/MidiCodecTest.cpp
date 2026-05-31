#include <iostream>
#include <cassert>
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

int main() {
    testLengthFromStatus();
    testNoteName();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
