// USB host MIDI transport for ESP32-S3 native OTG.
// Based on ESP32_Host_MIDI (Saulo Veríssimo), touchgadget esp32-usb-host-demos,
// and patterns from esp32-usb-host-midi-library / Omocha (MIT, enudenki).

#ifndef USB_CONNECTION_H
#define USB_CONNECTION_H

#include <Arduino.h>
#include <usb/usb_host.h>
#include <freertos/portmacro.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "MidiCodec.h"
#include "Transport.h"

class Board;

struct RawUsbMessage {
    uint8_t data[64];
    size_t length;
};

class USBConnection : public Transport {
public:
    static constexpr int kNumMidiInTransfers = 2;
    static constexpr int kMidiOutQueueDepth = 128;

    USBConnection();

    bool begin(Board* board = nullptr);

    void task() override;

    const char* name() const override { return "USB-HOST"; }
    TransportKind kind() const override { return TransportKind::kUsbHost; }
    bool isConnected() const override { return isReady; }
    bool canSend() const override { return isReady && outTransfer != nullptr && midiOutQueue != nullptr; }
    bool isPrimaryInbound() const override { return true; }
    bool sendMidi(const uint8_t* packet, size_t length) override { return sendMidiMessage(packet, length); }

    bool sendMidiMessage(const uint8_t* data, size_t length);

    const String& getLastError() const { return lastError; }
    TaskHandle_t getTaskHandle() const { return usbTaskHandle; }
    const String& getDeviceName() const { return deviceName; }
    uint16_t getVendorId() const { return vendorId_; }
    uint16_t getProductId() const { return productId_; }
    bool hasSeenDevice() const { return deviceSeen_; }
    uint32_t getRawUsbPacketsSeen() const { return rawUsbPacketsSeen_; }
    uint32_t getDecodedMidiPacketsSeen() const { return decodedMidiPacketsSeen_; }
    uint32_t getDecodeDropCount() const { return decodeDropCount_; }
    uint8_t getLastRawStatus() const { return lastRawStatus_; }
    uint8_t getMidiInEndpoint() const { return midiInEndpoint_; }
    uint8_t getMidiOutEndpoint() const { return midiOutEndpoint_; }
    uint8_t getClaimedInterfaceClass() const { return claimedInterfaceClass_; }
    uint8_t getClaimedInterfaceSubClass() const { return claimedInterfaceSubClass_; }
    const String& getDescriptorSummary() const { return descriptorSummary_; }
    int8_t getMidiInterfaceNumber() const { return midiInterfaceNumber; }
    bool isVendorByteStreamMode() const { return vendorByteStreamMode_; }
    uint8_t getLastUsbByte(uint8_t index) const { return index < sizeof(lastUsbBytes_) ? lastUsbBytes_[index] : 0; }
    uint8_t getLastUsbByteCount() const { return lastUsbByteCount_; }

    virtual void onMidiDataReceived(const uint8_t* data, size_t length);
    virtual void onDeviceConnected();
    virtual void onDeviceDisconnected();

    int getQueueSize() const;
    const RawUsbMessage& getQueueMessage(int index) const;

protected:
    volatile bool isReady;
    uint8_t interval;
    unsigned long lastCheck;

    usb_host_client_handle_t clientHandle;
    usb_device_handle_t deviceHandle;
    uint32_t eventFlags;
    usb_transfer_t* midiInTransfers[kNumMidiInTransfers];
    usb_transfer_t* outTransfer;
    QueueHandle_t midiOutQueue;
    volatile bool outTransferBusy;
    portMUX_TYPE outMux;

    static constexpr int QUEUE_SIZE = 64;
    RawUsbMessage usbQueue[QUEUE_SIZE];
    volatile int queueHead;
    volatile int queueTail;
    portMUX_TYPE queueMux;

    bool firstMidiReceived;
    volatile bool deviceSeen_;
    volatile uint32_t rawUsbPacketsSeen_;
    volatile uint32_t decodedMidiPacketsSeen_;
    volatile uint32_t decodeDropCount_;
    volatile uint8_t lastRawStatus_;
    uint8_t usbRunningStatus_[16];
    MidiCodec::Parser vendorStreamParser_;
    int8_t midiInterfaceNumber;
    int8_t midiAlternateSetting_;
    uint16_t vendorId_;
    uint16_t productId_;
    uint8_t midiInEndpoint_;
    uint8_t midiOutEndpoint_;
    uint8_t claimedInterfaceClass_;
    uint8_t claimedInterfaceSubClass_;
    uint8_t fallbackInterfaceNumber_;
    uint8_t fallbackAlternateSetting_;
    uint8_t fallbackInEndpoint_;
    uint8_t fallbackOutEndpoint_;
    uint8_t lastUsbBytes_[8];
    volatile uint8_t lastUsbByteCount_;
    bool vendorByteStreamMode_;
    String deviceName;
    String lastError;
    String descriptorSummary_;
    Board* board_;

    bool enqueueMidiMessage(const uint8_t* data, size_t length);
    bool dequeueMidiMessage(RawUsbMessage& msg);
    void processQueue();
    void processMidiOutQueue();

    TaskHandle_t usbTaskHandle;
    static void _usbTask(void* arg);

    static void _clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
    static void _onReceive(usb_transfer_t* transfer);
    static void _onSendComplete(usb_transfer_t* transfer);
    static void _onVendorStreamMidi(uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg);

    void _parseConfig(const usb_config_desc_t* config_desc);
    bool _isMidiInterface(const usb_intf_desc_t* intf) const;
    bool _isKnownVendorMidiDevice() const;
    bool _isKnownVendorMidiInterface(const usb_intf_desc_t* intf) const;
    bool _claimInterfaceAndSetupEndpoints(const usb_intf_desc_t* intf, const uint8_t* config, uint16_t totalLength, uint16_t indexAfterIntf, bool vendorByteStream);
    void _setupMidiEndpoint(const usb_ep_desc_t* endpoint, bool allowInterrupt);
    void _setupMidiInEndpoint(const usb_ep_desc_t* endpoint);
    void _setupMidiOutEndpoint(const usb_ep_desc_t* endpoint);
    bool _tryEndpointFallback();
    void loadDeviceInfo();
    void handleDeviceRemoved();
};

#endif
