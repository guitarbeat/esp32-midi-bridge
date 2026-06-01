#include "USBConnection.h"
#include "Board.h"
#include "BridgeLog.h"
#include "MidiCodec.h"

#include <string.h>
#include <usb/usb_helpers.h>

namespace {

String utf16DescriptorToAscii(const usb_str_desc_t* desc)
{
    if (desc == nullptr || desc->bLength < 2) {
        return String();
    }

    const uint8_t charCount = (desc->bLength - 2) / 2;
    String out;
    out.reserve(charCount);
    for (uint8_t i = 0; i < charCount; i++) {
        const char c = static_cast<char>(desc->wData[i] & 0xFF);
        if (c == '\0') {
            break;
        }
        out += c;
    }
    return out;
}

#ifndef DEBUG_USB
#define DEBUG_USB 0
#endif

#if DEBUG_USB
#define USB_LOG(...) BRIDGE_LOG(__VA_ARGS__)
#define USB_LOG_LN(msg) BRIDGE_LOG_LN(msg)
#else
#define USB_LOG(...) ((void)0)
#define USB_LOG_LN(msg) ((void)0)
#endif

}  // namespace

void USBConnection::loadDeviceInfo()
{
    deviceName = "";
    vendorId_ = 0;
    productId_ = 0;
    if (deviceHandle == nullptr) {
        return;
    }

    const usb_device_desc_t* devDesc = nullptr;
    if (usb_host_get_device_descriptor(deviceHandle, &devDesc) == ESP_OK && devDesc != nullptr) {
        vendorId_ = devDesc->idVendor;
        productId_ = devDesc->idProduct;
        BRIDGE_LOG("[USB] Device VID:PID %04X:%04X class 0x%02X subclass 0x%02X protocol 0x%02X\n",
                   vendorId_, productId_,
                   devDesc->bDeviceClass, devDesc->bDeviceSubClass, devDesc->bDeviceProtocol);
    }

    usb_device_info_t devInfo = {};
    if (usb_host_device_info(deviceHandle, &devInfo) != ESP_OK) {
        return;
    }

    deviceName = utf16DescriptorToAscii(devInfo.str_desc_product);
    if (deviceName.length() == 0) {
        deviceName = utf16DescriptorToAscii(devInfo.str_desc_manufacturer);
    }

    if (deviceName.length() > 0) {
        USB_LOG("[USB] Device name: %s\n", deviceName.c_str());
    }
}

USBConnection::USBConnection()
  : isReady(false),
    interval(0),
    lastCheck(0),
    clientHandle(nullptr),
    deviceHandle(nullptr),
    eventFlags(0),
    midiInTransfers{nullptr, nullptr},
    outTransfer(nullptr),
    midiOutQueue(nullptr),
    outTransferBusy(false),
    outMux(portMUX_INITIALIZER_UNLOCKED),
    queueHead(0),
    queueTail(0),
    queueMux(portMUX_INITIALIZER_UNLOCKED),
    firstMidiReceived(false),
    deviceSeen_(false),
    rawUsbPacketsSeen_(0),
    decodedMidiPacketsSeen_(0),
    decodeDropCount_(0),
    lastRawStatus_(0),
    vendorStreamParser_(_onVendorStreamMidi, this),
    midiInterfaceNumber(-1),
    midiAlternateSetting_(-1),
    vendorId_(0),
    productId_(0),
    vendorByteStreamMode_(false),
    deviceName(""),
    lastError(""),
    board_(nullptr),
    usbTaskHandle(nullptr)
{
    memset(usbRunningStatus_, 0, sizeof(usbRunningStatus_));
}

bool USBConnection::begin(Board* board)
{
    board_ = board;

    midiOutQueue = xQueueCreate(kMidiOutQueueDepth, sizeof(uint8_t[4]));
    if (midiOutQueue == nullptr) {
        lastError = "MIDI OUT queue create failed";
        return false;
    }

    usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&config);
    if (err != ESP_OK) {
        lastError = "USB host install failed (err=" + String(err) + ")";
        return false;
    }

    if (board_ != nullptr) {
        board_->enableUsbHostPower();
        BRIDGE_LOG_LN("[USB] Host power rails enabled (USB_SEL/VBUS)");
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = true,
        .max_num_event_msg = 10,
        .async = {
            .client_event_callback = _clientEventCallback,
            .callback_arg = this,
        }
    };
    err = usb_host_client_register(&client_config, &clientHandle);
    if (err != ESP_OK) {
        lastError = "USB client register failed (err=" + String(err) + ")";
        return false;
    }

    xTaskCreatePinnedToCore(_usbTask, "usb_midi", 4096, this, 11, &usbTaskHandle, 0);

    lastError = "";
    BRIDGE_LOG_LN("[USB] Host initialized");
    return true;
}

void USBConnection::task()
{
    processQueue();
    processMidiOutQueue();
}

void USBConnection::onDeviceConnected() {}
void USBConnection::onDeviceDisconnected() {}

void USBConnection::handleDeviceRemoved()
{
    isReady = false;
    outTransferBusy = false;

    for (int i = 0; i < kNumMidiInTransfers; i++) {
        if (midiInTransfers[i] != nullptr) {
            usb_host_transfer_free(midiInTransfers[i]);
            midiInTransfers[i] = nullptr;
        }
    }
    if (outTransfer != nullptr) {
        usb_host_transfer_free(outTransfer);
        outTransfer = nullptr;
    }
    if (midiOutQueue != nullptr) {
        xQueueReset(midiOutQueue);
    }

    if (deviceHandle != nullptr) {
        if (midiInterfaceNumber >= 0) {
            usb_host_interface_release(clientHandle, deviceHandle, static_cast<uint8_t>(midiInterfaceNumber));
            midiInterfaceNumber = -1;
            midiAlternateSetting_ = -1;
        }
        usb_host_device_close(clientHandle, deviceHandle);
        deviceHandle = nullptr;
    }

    portENTER_CRITICAL(&queueMux);
    queueHead = 0;
    queueTail = 0;
    portEXIT_CRITICAL(&queueMux);

    deviceName = "";
    firstMidiReceived = false;
    deviceSeen_ = false;
    rawUsbPacketsSeen_ = 0;
    decodedMidiPacketsSeen_ = 0;
    decodeDropCount_ = 0;
    lastRawStatus_ = 0;
    vendorId_ = 0;
    productId_ = 0;
    vendorByteStreamMode_ = false;
    vendorStreamParser_ = MidiCodec::Parser(_onVendorStreamMidi, this);
    memset(usbRunningStatus_, 0, sizeof(usbRunningStatus_));
    lastError = "";

    BRIDGE_LOG_LN("[USB] Keyboard unplugged — plug in again (BLE stays up)");
    onDeviceDisconnected();
}

void USBConnection::onMidiDataReceived(const uint8_t* data, size_t length)
{
    (void)data;
    (void)length;
}

bool USBConnection::enqueueMidiMessage(const uint8_t* data, size_t length)
{
    if (data == nullptr || length == 0 || length > sizeof(usbQueue[0].data)) {
        return false;
    }

    portENTER_CRITICAL(&queueMux);
    const int next = (queueHead + 1) % QUEUE_SIZE;
    if (next == queueTail) {
        portEXIT_CRITICAL(&queueMux);
        return false;
    }
    memcpy(usbQueue[queueHead].data, data, length);
    usbQueue[queueHead].length = length;
    queueHead = next;
    portEXIT_CRITICAL(&queueMux);
    return true;
}

bool USBConnection::dequeueMidiMessage(RawUsbMessage& msg)
{
    portENTER_CRITICAL(&queueMux);
    if (queueTail == queueHead) {
        portEXIT_CRITICAL(&queueMux);
        return false;
    }
    msg = usbQueue[queueTail];
    queueTail = (queueTail + 1) % QUEUE_SIZE;
    portEXIT_CRITICAL(&queueMux);
    return true;
}

void USBConnection::processQueue()
{
    RawUsbMessage msg;
    while (dequeueMidiMessage(msg)) {
        if (vendorByteStreamMode_) {
            if (msg.length >= 4 && (msg.length % 4) == 0) {
                uint8_t runningCopy[16];
                memcpy(runningCopy, usbRunningStatus_, sizeof(runningCopy));

                uint8_t decoded[16][3];
                size_t decodedLengths[16];
                size_t decodedCount = 0;
                bool usbMidiPacketized = true;

                for (size_t offset = 0; offset + 4 <= msg.length; offset += 4) {
                    if (msg.data[offset] == 0x00) {
                        continue;
                    }
                    size_t rawLen = 0;
                    if (!MidiCodec::decodeUsbEventToRaw(msg.data + offset, runningCopy, decoded[decodedCount], &rawLen)) {
                        usbMidiPacketized = false;
                        break;
                    }
                    decodedLengths[decodedCount] = rawLen;
                    decodedCount++;
                }

                if (usbMidiPacketized && decodedCount > 0) {
                    memcpy(usbRunningStatus_, runningCopy, sizeof(usbRunningStatus_));
                    for (size_t i = 0; i < decodedCount; i++) {
                        decodedMidiPacketsSeen_++;
                        lastRawStatus_ = decoded[i][0];
                        if (receiveCallback_) {
                            receiveCallback_(decoded[i], decodedLengths[i]);
                        }
                    }
                    continue;
                }
            }

            vendorStreamParser_.parse(msg.data, msg.length);
            continue;
        }

        if (msg.length < 4) {
            continue;
        }

        uint8_t raw[4];
        size_t rawLen = 0;
        if (!MidiCodec::decodeUsbEventToRaw(msg.data, usbRunningStatus_, raw, &rawLen)) {
            decodeDropCount_++;
            continue;
        }
        decodedMidiPacketsSeen_++;
        lastRawStatus_ = raw[0];

        if (receiveCallback_) {
            receiveCallback_(raw, rawLen);
        }
    }
}

int USBConnection::getQueueSize() const
{
    int size = queueHead - queueTail;
    if (size < 0) {
        size += QUEUE_SIZE;
    }
    return size;
}

const RawUsbMessage& USBConnection::getQueueMessage(int index) const
{
    const int realIndex = (queueTail + index) % QUEUE_SIZE;
    return usbQueue[realIndex];
}

void USBConnection::_usbTask(void* arg)
{
    USBConnection* usbCon = static_cast<USBConnection*>(arg);
    for (;;) {
        usb_host_lib_handle_events(pdMS_TO_TICKS(1), &usbCon->eventFlags);

        if (usbCon->eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            USB_LOG_LN("[USB] All devices free, ready for reconnect.");
        }
        if (usbCon->eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }

        usb_host_client_handle_events(usbCon->clientHandle, pdMS_TO_TICKS(1));

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void USBConnection::_clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg)
{
    USBConnection* usbCon = static_cast<USBConnection*>(arg);
    esp_err_t err;

    switch (eventMsg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            usbCon->deviceSeen_ = true;
            usbCon->rawUsbPacketsSeen_ = 0;
            usbCon->decodedMidiPacketsSeen_ = 0;
            usbCon->decodeDropCount_ = 0;
            usbCon->lastRawStatus_ = 0;
            BRIDGE_LOG("[USB] New device detected at address %d\n", eventMsg->new_dev.address);
            err = usb_host_device_open(usbCon->clientHandle, eventMsg->new_dev.address, &usbCon->deviceHandle);
            if (err != ESP_OK) {
                usbCon->lastError = "Device open failed (err=" + String(err) + ")";
                BRIDGE_LOG("[USB] %s\n", usbCon->lastError.c_str());
                return;
            }
            usbCon->loadDeviceInfo();
            {
                const usb_config_desc_t* config_desc;
                err = usb_host_get_active_config_descriptor(usbCon->deviceHandle, &config_desc);
                if (err != ESP_OK) {
                    usbCon->lastError = "Config descriptor failed (err=" + String(err) + ")";
                    BRIDGE_LOG("[USB] %s\n", usbCon->lastError.c_str());
                    return;
                }
                BRIDGE_LOG("[USB] Active config length: %d\n", config_desc->wTotalLength);
                usbCon->_parseConfig(config_desc);
            }
            if (usbCon->isReady) {
                usbCon->lastError = "";
                BRIDGE_LOG("[USB] MIDI device '%s' ready.\n", usbCon->deviceName.c_str());
                usbCon->onDeviceConnected();
            } else if (usbCon->lastError.length() == 0) {
                usbCon->lastError = "No MIDI interface found";
                BRIDGE_LOG_LN("[USB] Error: No MIDI interface found. Check if the device is in 'Generic' USB mode.");
            } else {
                BRIDGE_LOG("[USB] Error: %s\n", usbCon->lastError.c_str());
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            BRIDGE_LOG_LN("[USB] Device removed.");
            usbCon->handleDeviceRemoved();
            break;
        default:
            break;
    }
}

void USBConnection::_onReceive(usb_transfer_t* transfer)
{
    USBConnection* usbCon = static_cast<USBConnection*>(transfer->context);

    if (transfer->status == 0 &&
        ((usbCon->vendorByteStreamMode_ && transfer->actual_num_bytes > 0) ||
         (!usbCon->vendorByteStreamMode_ && transfer->actual_num_bytes >= 4))) {
        if (usbCon->vendorByteStreamMode_) {
            const size_t length = min(static_cast<size_t>(transfer->actual_num_bytes),
                                      sizeof(usbCon->usbQueue[0].data));
            if (length > 0 && usbCon->enqueueMidiMessage(transfer->data_buffer, length)) {
                usbCon->rawUsbPacketsSeen_++;
                if (!usbCon->firstMidiReceived) {
                    usbCon->firstMidiReceived = true;
                    BRIDGE_LOG("[USB] First Roland vendor USB data received (%u bytes)\n",
                               static_cast<unsigned>(length));
                }
            }
        } else {
            for (int offset = 0; offset + 4 <= transfer->actual_num_bytes; offset += 4) {
                if (transfer->data_buffer[offset] == 0x00) {
                    continue;
                }

                if (usbCon->enqueueMidiMessage(transfer->data_buffer + offset, 4)) {
                    usbCon->rawUsbPacketsSeen_++;
                    if (!usbCon->firstMidiReceived) {
                        usbCon->firstMidiReceived = true;
                        const uint8_t status = transfer->data_buffer[offset + 1];
                        BRIDGE_LOG("[USB] First MIDI data received (status=0x%02X)\n", status);
                    }
                }
            }
        }
    } else if (transfer->status != 0) {
        USB_LOG("[USB] Transfer error: %d\n", transfer->status);
    }

    if (usbCon->isReady) {
        const esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            USB_LOG("[USB] Transfer re-submit failed: %d\n", err);
        }
    }
}

static uint8_t midiStatusToCin(uint8_t status)
{
    if (status >= 0x80 && status <= 0xEF) {
        return status >> 4;
    }

    switch (status) {
        case 0xF0: return 0x04;
        case 0xF1: return 0x02;
        case 0xF2: return 0x03;
        case 0xF3: return 0x02;
        case 0xF6: return 0x05;
        case 0xF7: return 0x05;
        case 0xF8:
        case 0xFA:
        case 0xFB:
        case 0xFC:
        case 0xFE:
        case 0xFF:
            return 0x0F;
        default:
            return 0;
    }
}

bool USBConnection::sendMidiMessage(const uint8_t* data, size_t length)
{
    if (!isReady || midiOutQueue == nullptr || data == nullptr || length == 0) {
        return false;
    }

    const uint8_t cin = midiStatusToCin(data[0]);
    if (cin == 0) {
        return false;
    }

    uint8_t packet[4] = {cin, 0, 0, 0};
    for (size_t i = 0; i < length && i < 3; i++) {
        packet[i + 1] = data[i];
    }

    if (xQueueSend(midiOutQueue, packet, 0) != pdPASS) {
        return false;
    }

    return true;
}

void USBConnection::processMidiOutQueue()
{
    if (!isReady || outTransfer == nullptr || midiOutQueue == nullptr) {
        return;
    }

    portENTER_CRITICAL(&outMux);
    if (outTransferBusy) {
        portEXIT_CRITICAL(&outMux);
        return;
    }
    outTransferBusy = true;
    portEXIT_CRITICAL(&outMux);

    if (uxQueueMessagesWaiting(midiOutQueue) == 0) {
        portENTER_CRITICAL(&outMux);
        outTransferBusy = false;
        portEXIT_CRITICAL(&outMux);
        return;
    }

    size_t bytesToSend = 0;
    uint8_t tempMessage[4];
    const size_t maxPacketSize = outTransfer->data_buffer_size;

    while (bytesToSend + 4 <= maxPacketSize && uxQueueMessagesWaiting(midiOutQueue) > 0) {
        if (xQueueReceive(midiOutQueue, tempMessage, 0) == pdPASS) {
            memcpy(outTransfer->data_buffer + bytesToSend, tempMessage, 4);
            bytesToSend += 4;
        }
    }

    if (bytesToSend == 0) {
        portENTER_CRITICAL(&outMux);
        outTransferBusy = false;
        portEXIT_CRITICAL(&outMux);
        return;
    }

    outTransfer->num_bytes = bytesToSend;
    const esp_err_t err = usb_host_transfer_submit(outTransfer);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&outMux);
        outTransferBusy = false;
        portEXIT_CRITICAL(&outMux);
        USB_LOG("[USB] OUT submit failed: %d\n", err);
    }
}

void USBConnection::_onSendComplete(usb_transfer_t* transfer)
{
    USBConnection* usbCon = static_cast<USBConnection*>(transfer->context);
    if (usbCon == nullptr) {
        return;
    }
    portENTER_CRITICAL(&usbCon->outMux);
    usbCon->outTransferBusy = false;
    portEXIT_CRITICAL(&usbCon->outMux);
}

void USBConnection::_onVendorStreamMidi(uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg)
{
    (void)sysexPos;
    USBConnection* usbCon = static_cast<USBConnection*>(arg);
    if (usbCon == nullptr || usbCon->receiveCallback_ == nullptr) {
        return;
    }

    if (status == 0xF0 || status == 0xF7 || length > 2) {
        usbCon->decodeDropCount_++;
        return;
    }

    uint8_t raw[3] = {status, 0, 0};
    for (size_t i = 0; i < length; i++) {
        raw[i + 1] = data[i];
    }

    usbCon->decodedMidiPacketsSeen_++;
    usbCon->lastRawStatus_ = status;
    usbCon->receiveCallback_(raw, length + 1);
}

bool USBConnection::_isMidiInterface(const usb_intf_desc_t* intf) const
{
    if (intf == nullptr) {
        return false;
    }

    const bool isStandardMidi = (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                                 intf->bInterfaceSubClass == 0x03 &&
                                 intf->bNumEndpoints > 0);
    const bool isLegacyAudio = (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                                intf->bInterfaceSubClass == 0x00 &&
                                intf->bNumEndpoints > 0);
    return isStandardMidi || isLegacyAudio;
}

bool USBConnection::_isKnownVendorMidiDevice() const
{
    static constexpr uint16_t kRolandVendorId = 0x0582;
    static constexpr uint16_t kRolandF20ProductId = 0x0122;

    return vendorId_ == kRolandVendorId && productId_ == kRolandF20ProductId;
}

bool USBConnection::_isKnownVendorMidiInterface(const usb_intf_desc_t* intf) const
{
    return _isKnownVendorMidiDevice() &&
           intf != nullptr &&
           intf->bInterfaceClass == 0xFF &&
           intf->bNumEndpoints > 0;
}

bool USBConnection::_claimInterfaceAndSetupEndpoints(const usb_intf_desc_t* intf,
                                                     const uint8_t* config,
                                                     uint16_t totalLength,
                                                     uint16_t indexAfterIntf,
                                                     bool vendorByteStream)
{
    if (intf == nullptr || isReady || config == nullptr) {
        return false;
    }

    BRIDGE_LOG("[USB] Claiming interface %d alt %d (%d endpoints)...\n",
               intf->bInterfaceNumber, intf->bAlternateSetting, intf->bNumEndpoints);

    const esp_err_t err = usb_host_interface_claim(clientHandle, deviceHandle,
                                                   intf->bInterfaceNumber, intf->bAlternateSetting);
    if (err != ESP_OK) {
        BRIDGE_LOG("[USB] Failed to claim interface %d alt %d (err=%d)\n",
                   intf->bInterfaceNumber, intf->bAlternateSetting, err);
        return false;
    }

    uint16_t idx = indexAfterIntf;
    while (idx < totalLength) {
        if (idx + 1 >= totalLength) {
            break;
        }

        const uint8_t len = config[idx];
        if (len < 2 || (idx + len) > totalLength) {
            break;
        }

        const uint8_t descriptorType = config[idx + 1];
        if (descriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            break;
        }

        if (descriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const auto* ep = reinterpret_cast<const usb_ep_desc_t*>(&config[idx]);
            _setupMidiEndpoint(ep, vendorByteStream);
        }

        idx += len;
    }

    if (midiInTransfers[0] == nullptr) {
        usb_host_interface_release(clientHandle, deviceHandle, intf->bInterfaceNumber);
        for (int i = 0; i < kNumMidiInTransfers; i++) {
            if (midiInTransfers[i] != nullptr) {
                usb_host_transfer_free(midiInTransfers[i]);
                midiInTransfers[i] = nullptr;
            }
        }
        if (outTransfer != nullptr) {
            usb_host_transfer_free(outTransfer);
            outTransfer = nullptr;
            outTransferBusy = false;
        }
        return false;
    }

    midiInterfaceNumber = static_cast<int8_t>(intf->bInterfaceNumber);
    midiAlternateSetting_ = static_cast<int8_t>(intf->bAlternateSetting);
    vendorByteStreamMode_ = vendorByteStream;
    return true;
}

void USBConnection::_parseConfig(const usb_config_desc_t* config_desc)
{
    if (config_desc == nullptr) {
        return;
    }

    const uint8_t* p = config_desc->val;
    const uint16_t totalLength = config_desc->wTotalLength;
    uint16_t index = 0;
    bool claimedOk = false;
    bool vendorModeSeen = false;

    BRIDGE_LOG("[USB] Scanning %d bytes of descriptors...\n", totalLength);
    if (_isKnownVendorMidiDevice()) {
        BRIDGE_LOG_LN("[USB] Roland F-20 VID/PID matched; accepting vendor MIDI interface.");
    }

    while (index < totalLength) {
        if (index + 1 >= totalLength) {
            break;
        }

        const uint8_t len = p[index];
        if (len < 2 || (index + len) > totalLength) {
            break;
        }

        const uint8_t descriptorType = p[index + 1];

        if (descriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const auto* intf = reinterpret_cast<const usb_intf_desc_t*>(&p[index]);
            BRIDGE_LOG("[USB] Found Interface %d alt %d: Class 0x%02X, SubClass 0x%02X, Endpoints %d\n",
                       intf->bInterfaceNumber, intf->bAlternateSetting,
                       intf->bInterfaceClass, intf->bInterfaceSubClass, intf->bNumEndpoints);

            const bool knownVendorMidi = _isKnownVendorMidiInterface(intf);
            if (intf->bInterfaceClass == 0xFF && intf->bNumEndpoints > 0) {
                vendorModeSeen = true;
                if (knownVendorMidi) {
                    lastError = "Roland F-20 vendor MIDI";
                    BRIDGE_LOG_LN("[USB] Roland vendor-class MIDI candidate found.");
                } else {
                    lastError = "Vendor USB MIDI mode unsupported";
                    BRIDGE_LOG_LN("[USB] Vendor-class interface seen; generic USB MIDI descriptors not present.");
                }
            }

            if (!isReady && _isMidiInterface(intf)) {
                if (_claimInterfaceAndSetupEndpoints(intf, p, totalLength, index + len, false)) {
                    claimedOk = true;
                    break;
                }
            }

            if (!isReady && knownVendorMidi) {
                if (_claimInterfaceAndSetupEndpoints(intf, p, totalLength, index + len, true)) {
                    claimedOk = true;
                    break;
                }
            }
        }

        index += len;
    }

    if (claimedOk) {
        isReady = true;
        BRIDGE_LOG_LN("[USB]   MIDI IN/OUT configured. Applying Casio CT-S1 delay...");
        vTaskDelay(pdMS_TO_TICKS(200));

        for (int i = 0; i < kNumMidiInTransfers; i++) {
            if (midiInTransfers[i] != nullptr) {
                usb_host_transfer_submit(midiInTransfers[i]);
            }
        }
        return;
    }

    if (!claimedOk && (!vendorModeSeen || _isKnownVendorMidiDevice())) {
        claimedOk = _tryEndpointFallback();
    }

    if (!claimedOk && lastError.length() == 0) {
        lastError = "No USB MIDI interface found";
    }
}

void USBConnection::_setupMidiInEndpoint(const usb_ep_desc_t* endpoint)
{
    if (endpoint == nullptr) {
        return;
    }

    uint16_t wMaxPacketSize = endpoint->wMaxPacketSize;
    if (wMaxPacketSize > 512) {
        wMaxPacketSize = 512;
    }
    if (wMaxPacketSize == 0) {
        wMaxPacketSize = 64;
    }

    for (int i = 0; i < kNumMidiInTransfers; i++) {
        if (midiInTransfers[i] != nullptr) {
            continue;
        }

        esp_err_t err = usb_host_transfer_alloc(wMaxPacketSize, 3000, &midiInTransfers[i]);
        if (err != ESP_OK || midiInTransfers[i] == nullptr) {
            lastError = "MIDI IN alloc failed (err=" + String(err) + ")";
            return;
        }

        midiInTransfers[i]->device_handle = deviceHandle;
        midiInTransfers[i]->bEndpointAddress = endpoint->bEndpointAddress;
        midiInTransfers[i]->callback = _onReceive;
        midiInTransfers[i]->context = this;
        midiInTransfers[i]->num_bytes = wMaxPacketSize;
        interval = (endpoint->bInterval == 0) ? 1 : endpoint->bInterval;

        BRIDGE_LOG("[USB]   MIDI IN allocated on 0x%02X (pipe %d)\n",
                   endpoint->bEndpointAddress, i);
        return;
    }
}

void USBConnection::_setupMidiOutEndpoint(const usb_ep_desc_t* endpoint)
{
    if (endpoint == nullptr || outTransfer != nullptr) {
        return;
    }

    uint16_t wMaxPacketSize = endpoint->wMaxPacketSize;
    if (wMaxPacketSize > 512) {
        wMaxPacketSize = 512;
    }
    if (wMaxPacketSize == 0) {
        wMaxPacketSize = 64;
    }

    const esp_err_t err = usb_host_transfer_alloc(wMaxPacketSize, 3000, &outTransfer);
    if (err != ESP_OK || outTransfer == nullptr) {
        return;
    }

    outTransfer->device_handle = deviceHandle;
    outTransfer->bEndpointAddress = endpoint->bEndpointAddress;
    outTransfer->callback = _onSendComplete;
    outTransfer->context = this;
    outTransfer->num_bytes = wMaxPacketSize;
    BRIDGE_LOG("[USB]   MIDI OUT allocated on 0x%02X\n", endpoint->bEndpointAddress);
}

void USBConnection::_setupMidiEndpoint(const usb_ep_desc_t* endpoint, bool allowInterrupt)
{
    if (endpoint == nullptr) {
        return;
    }

    const uint8_t transferType = endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
    const bool supportedTransfer = transferType == USB_BM_ATTRIBUTES_XFER_BULK ||
                                   (allowInterrupt && transferType == USB_BM_ATTRIBUTES_XFER_INT);
    if (!supportedTransfer) {
        return;
    }

    BRIDGE_LOG("[USB]   Endpoint 0x%02X: Attr 0x%02X, MaxPacket %d\n",
               endpoint->bEndpointAddress, endpoint->bmAttributes, endpoint->wMaxPacketSize);

    if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
        _setupMidiInEndpoint(endpoint);
    } else {
        _setupMidiOutEndpoint(endpoint);
    }
}

bool USBConnection::_tryEndpointFallback()
{
    BRIDGE_LOG_LN("[USB] MIDI descriptors not found. Attempting aggressive fallback (Endpoint 0x81)...");

    esp_err_t err = usb_host_transfer_alloc(64, 3000, &midiInTransfers[0]);
    if (err != ESP_OK || midiInTransfers[0] == nullptr) {
        return false;
    }

    midiInTransfers[0]->device_handle = deviceHandle;
    midiInTransfers[0]->bEndpointAddress = 0x81;
    midiInTransfers[0]->callback = _onReceive;
    midiInTransfers[0]->context = this;
    midiInTransfers[0]->num_bytes = 64;
    interval = 1;
    vendorByteStreamMode_ = _isKnownVendorMidiDevice();
    isReady = true;

    vTaskDelay(pdMS_TO_TICKS(200));
    usb_host_transfer_submit(midiInTransfers[0]);
    BRIDGE_LOG_LN("[USB] Fallback connected on 0x81.");
    return true;
}
