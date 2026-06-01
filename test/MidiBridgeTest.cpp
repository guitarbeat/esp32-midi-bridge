#include <cassert>
#include <iostream>
#include <vector>

#include "../firmware/bridge-s3/MidiBridge.h"
#include "../firmware/bridge-s3/MidiEngine.h"

class FakeTransport : public Transport {
public:
    FakeTransport(const char* transportName, bool primaryIn = false, bool primaryOut = false)
        : name_(transportName), primaryIn_(primaryIn), primaryOut_(primaryOut) {}

    const char* name() const override { return name_; }
    bool isConnected() const override { return connected_; }
    bool isPrimaryInbound() const override { return primaryIn_; }
    bool isPrimaryOutbound() const override { return primaryOut_; }

    bool sendMidi(const uint8_t* packet, size_t length) override
    {
        if (!sendOk_) {
            return false;
        }
        sent_.assign(packet, packet + length);
        sendCount_++;
        return true;
    }

    void setConnected(bool connected) { connected_ = connected; }
    void setSendOk(bool ok) { sendOk_ = ok; }
    const std::vector<uint8_t>& lastSent() const { return sent_; }
    int sendCount() const { return sendCount_; }

private:
    const char* name_;
    bool primaryIn_ = false;
    bool primaryOut_ = false;
    bool connected_ = false;
    bool sendOk_ = true;
    std::vector<uint8_t> sent_;
    int sendCount_ = 0;
};

void testForwardToPrimaryOutbound() {
    MidiBridge bridge;
    MidiEngine engine;
    FakeTransport usb("USB-HOST", true, false);
    FakeTransport ble("BLE-MIDI", false, true);
    ble.setConnected(true);

    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOn[] = {0x90, 60, 100};
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kForwarded);
    assert(ble.sendCount() == 1);
    assert(ble.lastSent()[0] == 0x90);
    assert(ble.lastSent()[1] == 60);
    assert(bridge.counters().usbPacketsSeen == 1);
    std::cout << "testForwardToPrimaryOutbound passed!" << std::endl;
}

void testChannelFilterBlocksForward() {
    MidiBridge bridge;
    MidiEngine engine;
    engine.setChannelFilter(1);
    FakeTransport usb("USB-HOST", true, false);
    FakeTransport ble("BLE-MIDI", false, true);
    ble.setConnected(true);
    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOnCh2[] = {0x91, 60, 100};
    assert(bridge.route(&usb, noteOnCh2, 3) == MidiBridge::Result::kFiltered);
    assert(ble.sendCount() == 0);
    std::cout << "testChannelFilterBlocksForward passed!" << std::endl;
}

void testTransposeAppliedBeforeSend() {
    MidiBridge bridge;
    MidiEngine engine;
    engine.setTranspose(2);
    FakeTransport usb("USB-HOST", true, false);
    FakeTransport ble("BLE-MIDI", false, true);
    ble.setConnected(true);
    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOn[] = {0x90, 60, 100};
    bridge.route(&usb, noteOn, 3);
    assert(ble.lastSent()[1] == 62);
    std::cout << "testTransposeAppliedBeforeSend passed!" << std::endl;
}

void testPauseBlocksForward() {
    MidiBridge bridge;
    MidiEngine engine;
    bool paused = true;
    FakeTransport usb("USB-HOST", true, false);
    FakeTransport ble("BLE-MIDI", false, true);
    ble.setConnected(true);
    bridge.begin(nullptr, [&paused]() { return paused; });
    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOn[] = {0x90, 60, 100};
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kFiltered);
    assert(ble.sendCount() == 0);

    paused = false;
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kForwarded);
    assert(ble.sendCount() == 1);
    std::cout << "testPauseBlocksForward passed!" << std::endl;
}

void testDisconnectedPrimaryOutboundSkips() {
    MidiBridge bridge;
    MidiEngine engine;
    FakeTransport usb("USB-HOST", true, false);
    FakeTransport ble("BLE-MIDI", false, true);
    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOn[] = {0x90, 60, 100};
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kFiltered);
    assert(bridge.counters().blePacketsSkipped == 1);
    std::cout << "testDisconnectedPrimaryOutboundSkips passed!" << std::endl;
}

int main() {
    testForwardToPrimaryOutbound();
    testChannelFilterBlocksForward();
    testTransposeAppliedBeforeSend();
    testPauseBlocksForward();
    testDisconnectedPrimaryOutboundSkips();
    std::cout << "All MidiBridge tests passed!" << std::endl;
    return 0;
}
