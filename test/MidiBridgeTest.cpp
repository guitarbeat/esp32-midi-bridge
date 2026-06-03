#include <cassert>
#include <iostream>
#include <vector>

#include "../firmware/bridge-s3/src/midi/MidiBridge.h"
#include "../firmware/bridge-s3/src/midi/MidiEngine.h"

class FakeTransport : public MidiTransport {
public:
    FakeTransport(const char* transportName, MidiTransportKind transportKind)
        : name_(transportName), kind_(transportKind) {}

    const char* name() const override { return name_; }
    MidiTransportKind kind() const override { return kind_; }
    bool isConnected() const override { return connected_; }
    bool canSend() const override { return connected_ && sendCapable_; }

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
    void setSendCapable(bool capable) { sendCapable_ = capable; }
    void setSendOk(bool ok) { sendOk_ = ok; }
    const std::vector<uint8_t>& lastSent() const { return sent_; }
    int sendCount() const { return sendCount_; }

private:
    const char* name_;
    MidiTransportKind kind_;
    bool connected_ = false;
    bool sendCapable_ = true;
    bool sendOk_ = true;
    std::vector<uint8_t> sent_;
    int sendCount_ = 0;
};

static const MidiBridge::RouteStats& stats(const MidiBridge& bridge, MidiTransportKind kind)
{
    return bridge.statsFor(kind);
}

void testUsbBroadcastsToBleRtpUart()
{
    MidiBridge bridge;
    MidiEngine engine;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    FakeTransport rtp("RTP-MIDI", MidiTransportKind::kRtp);
    FakeTransport uart("UART-MIDI", MidiTransportKind::kUart);
    usb.setConnected(true);
    ble.setConnected(true);
    rtp.setConnected(true);
    uart.setConnected(true);

    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);
    bridge.addTransport(&rtp);
    bridge.addTransport(&uart);

    const uint8_t noteOn[] = {0x90, 60, 100};
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kForwarded);
    assert(ble.sendCount() == 1);
    assert(rtp.sendCount() == 1);
    assert(uart.sendCount() == 1);
    assert(usb.sendCount() == 0);
    assert(stats(bridge, MidiTransportKind::kUsbHost).received == 1);
    assert(stats(bridge, MidiTransportKind::kBle).sent == 1);
    assert(stats(bridge, MidiTransportKind::kRtp).sent == 1);
    assert(stats(bridge, MidiTransportKind::kUart).sent == 1);
    std::cout << "testUsbBroadcastsToBleRtpUart passed!" << std::endl;
}

void testReverseRoutesToUsbOnlyWhenUsbOutAvailable()
{
    MidiBridge bridge;
    MidiEngine engine;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    usb.setConnected(true);
    usb.setSendCapable(false);
    ble.setConnected(true);

    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t noteOn[] = {0x90, 64, 90};
    assert(bridge.route(&ble, noteOn, 3) == MidiBridge::Result::kFiltered);
    assert(usb.sendCount() == 0);
    assert(stats(bridge, MidiTransportKind::kUsbHost).skipped == 1);

    usb.setSendCapable(true);
    assert(bridge.route(&ble, noteOn, 3) == MidiBridge::Result::kForwarded);
    assert(usb.sendCount() == 1);
    assert(stats(bridge, MidiTransportKind::kUsbHost).sent == 1);
    std::cout << "testReverseRoutesToUsbOnlyWhenUsbOutAvailable passed!" << std::endl;
}

void testSourceTransportNeverReceivesOwnMessage()
{
    MidiBridge bridge;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    usb.setConnected(true);
    ble.setConnected(true);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t cc[] = {0xB0, 64, 127};
    assert(bridge.route(&ble, cc, 3) == MidiBridge::Result::kForwarded);
    assert(ble.sendCount() == 0);
    assert(usb.sendCount() == 1);
    std::cout << "testSourceTransportNeverReceivesOwnMessage passed!" << std::endl;
}

void testBleDisconnectedDoesNotBlockRtpOrUart()
{
    MidiBridge bridge;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    FakeTransport rtp("RTP-MIDI", MidiTransportKind::kRtp);
    FakeTransport uart("UART-MIDI", MidiTransportKind::kUart);
    usb.setConnected(true);
    ble.setConnected(false);
    rtp.setConnected(true);
    uart.setConnected(true);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);
    bridge.addTransport(&rtp);
    bridge.addTransport(&uart);

    const uint8_t noteOff[] = {0x80, 60, 0};
    assert(bridge.route(&usb, noteOff, 3) == MidiBridge::Result::kForwarded);
    assert(ble.sendCount() == 0);
    assert(rtp.sendCount() == 1);
    assert(uart.sendCount() == 1);
    assert(stats(bridge, MidiTransportKind::kBle).skipped == 1);
    std::cout << "testBleDisconnectedDoesNotBlockRtpOrUart passed!" << std::endl;
}

void testActiveSenseAndMidiClockFilteredByDefault()
{
    MidiBridge bridge;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    usb.setConnected(true);
    ble.setConnected(true);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t activeSense[] = {0xFE};
    const uint8_t clock[] = {0xF8};
    assert(bridge.route(&usb, activeSense, 1) == MidiBridge::Result::kFiltered);
    assert(bridge.route(&usb, clock, 1) == MidiBridge::Result::kFiltered);
    assert(ble.sendCount() == 0);
    assert(bridge.counters().filteredActiveSense == 1);
    assert(bridge.counters().filteredClock == 1);
    std::cout << "testActiveSenseAndMidiClockFilteredByDefault passed!" << std::endl;
}

void testShortVoiceMessagesRouteUnchanged()
{
    MidiBridge bridge;
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    usb.setConnected(true);
    ble.setConnected(true);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);

    const uint8_t programChange[] = {0xC1, 12};
    const uint8_t channelPressure[] = {0xD2, 80};
    const uint8_t pitchBend[] = {0xE0, 0x00, 0x40};
    assert(bridge.route(&usb, programChange, 2) == MidiBridge::Result::kForwarded);
    assert(ble.lastSent() == std::vector<uint8_t>({0xC1, 12}));
    assert(bridge.route(&usb, channelPressure, 2) == MidiBridge::Result::kForwarded);
    assert(ble.lastSent() == std::vector<uint8_t>({0xD2, 80}));
    assert(bridge.route(&usb, pitchBend, 3) == MidiBridge::Result::kForwarded);
    assert(ble.lastSent() == std::vector<uint8_t>({0xE0, 0x00, 0x40}));
    std::cout << "testShortVoiceMessagesRouteUnchanged passed!" << std::endl;
}

void testChannelFilterAndTransposeApplyOnceBeforeBroadcast()
{
    MidiBridge bridge;
    MidiEngine engine;
    engine.setTranspose(2);
    engine.setChannelFilter(1);
    FakeTransport usb("USB-HOST", MidiTransportKind::kUsbHost);
    FakeTransport ble("BLE-MIDI", MidiTransportKind::kBle);
    FakeTransport rtp("RTP-MIDI", MidiTransportKind::kRtp);
    usb.setConnected(true);
    ble.setConnected(true);
    rtp.setConnected(true);
    bridge.setMidiEngine(&engine);
    bridge.addTransport(&usb);
    bridge.addTransport(&ble);
    bridge.addTransport(&rtp);

    const uint8_t wrongChannel[] = {0x91, 60, 100};
    assert(bridge.route(&usb, wrongChannel, 3) == MidiBridge::Result::kFiltered);
    assert(ble.sendCount() == 0);
    assert(rtp.sendCount() == 0);

    const uint8_t noteOn[] = {0x90, 60, 100};
    assert(bridge.route(&usb, noteOn, 3) == MidiBridge::Result::kForwarded);
    assert(ble.lastSent() == std::vector<uint8_t>({0x90, 62, 100}));
    assert(rtp.lastSent() == std::vector<uint8_t>({0x90, 62, 100}));
    std::cout << "testChannelFilterAndTransposeApplyOnceBeforeBroadcast passed!" << std::endl;
}

int main()
{
    testUsbBroadcastsToBleRtpUart();
    testReverseRoutesToUsbOnlyWhenUsbOutAvailable();
    testSourceTransportNeverReceivesOwnMessage();
    testBleDisconnectedDoesNotBlockRtpOrUart();
    testActiveSenseAndMidiClockFilteredByDefault();
    testShortVoiceMessagesRouteUnchanged();
    testChannelFilterAndTransposeApplyOnceBeforeBroadcast();
    std::cout << "All MidiBridge tests passed!" << std::endl;
    return 0;
}
