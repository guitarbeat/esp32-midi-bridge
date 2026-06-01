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
    uint8_t usbRunningStatus_[16];
    int8_t midiInterfaceNumber;
    int8_t midiAlternateSetting_;
    String deviceName;
    String lastError;
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

    void _parseConfig(const usb_config_desc_t* config_desc);
    bool _isMidiInterface(const usb_intf_desc_t* intf) const;
    bool _claimInterfaceAndSetupEndpoints(const usb_intf_desc_t* intf, const uint8_t* config, uint16_t totalLength, uint16_t indexAfterIntf);
    void _setupMidiEndpoint(const usb_ep_desc_t* endpoint);
    void _setupMidiInEndpoint(const usb_ep_desc_t* endpoint);
    void _setupMidiOutEndpoint(const usb_ep_desc_t* endpoint);
    bool _tryEndpointFallback();
    void loadDeviceName();
    void handleDeviceRemoved();
};

#endif
