#include <cassert>
#include <cstring>
#include <iostream>

#include "../firmware/bridge-s3/MidiEngine.h"

void testChannelFilterUsesStatusChannel() {
    MidiEngine engine;
    engine.setChannelFilter(1);

    uint8_t noteOnCh1[] = {0x90, 60, 100};
    assert(engine.processPacket(noteOnCh1, 3));

    uint8_t noteOnCh2[] = {0x91, 60, 100};
    assert(!engine.processPacket(noteOnCh2, 3));
    std::cout << "testChannelFilterUsesStatusChannel passed!" << std::endl;
}

void testTransposeModifiesNoteByte() {
    MidiEngine engine;
    engine.setTranspose(2);

    uint8_t noteOn[] = {0x90, 60, 100};
    assert(engine.processPacket(noteOn, 3));
    assert(noteOn[1] == 62);
    std::cout << "testTransposeModifiesNoteByte passed!" << std::endl;
}

void testProgramChangeLength() {
    MidiEngine engine;
    uint8_t pc[] = {0xC0, 5};
    assert(engine.processPacket(pc, 2));
    std::cout << "testProgramChangeLength passed!" << std::endl;
}

int main() {
    testChannelFilterUsesStatusChannel();
    testTransposeModifiesNoteByte();
    testProgramChangeLength();
    std::cout << "All MidiEngine tests passed!" << std::endl;
    return 0;
}
