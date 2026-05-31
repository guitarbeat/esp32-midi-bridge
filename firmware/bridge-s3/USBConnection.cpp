#include "USBConnection.h"
#include "Board.h"
#include "BridgeLog.h"

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

void USBConnection::loadDeviceName()
{
    deviceName = "";
    if (deviceHandle == nullptr) {
        return;
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
    queueHead(0),
    queueTail(0),
    queueMux(portMUX_INITIALIZER_UNLOCKED),
    firstMidiReceived(false),
    midiInterfaceNumber(-1),
    deviceName(""),
    lastError(""),
    board_(nullptr),
    usbTaskHandle(nullptr)
{
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
    lastError = "";

    BRIDGE_LOG_LN("[USB] Keyboard unplugged — plug in again (BLE stays up)");
    onDeviceDisconnected();
}

void USBConnection::onMidiDataReceived(const uint8_t* data, size_t length)
{
    (void)data;
    (void)length;
}

bool USBConnection::enqueueMidiMessage(const uint8_t* data, size_t /*length*/)
{
    portENTER_CRITICAL(&queueMux);
    const int next = (queueHead + 1) % QUEUE_SIZE;
    if (next == queueTail) {
        portEXIT_CRITICAL(&queueMux);
        return false;
    }
    memcpy(usbQueue[queueHead].data, data, 3);
    usbQueue[queueHead].length = 3;
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
        if (receiveCallback_) {
            receiveCallback_(msg.data, msg.length);
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
            BRIDGE_LOG("[USB] New device detected at address %d\n", eventMsg->new_dev.address);
            err = usb_host_device_open(usbCon->clientHandle, eventMsg->new_dev.address, &usbCon->deviceHandle);
            if (err != ESP_OK) {
                usbCon->lastError = "Device open failed (err=" + String(err) + ")";
                BRIDGE_LOG("[USB] %s\n", usbCon->lastError.c_str());
                return;
            }
            usbCon->loadDeviceName();
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

    if (transfer->status == 0 && transfer->actual_num_bytes >= 4) {
        for (int offset = 0; offset + 4 <= transfer->actual_num_bytes; offset += 4) {
            const uint8_t cin = transfer->data_buffer[offset] & 0x0F;
            const uint8_t status = transfer->data_buffer[offset + 1];

            if (cin == 0x00 && status == 0x00) {
                continue;
            }

            if (usbCon->enqueueMidiMessage(transfer->data_buffer + offset + 1, 3)) {
                if (!usbCon->firstMidiReceived) {
                    usbCon->firstMidiReceived = true;
                    BRIDGE_LOG("[USB] First MIDI data received (status=0x%02X)\n", status);
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

    processMidiOutQueue();
    return true;
}

void USBConnection::processMidiOutQueue()
{
    if (!isReady || outTransfer == nullptr || midiOutQueue == nullptr) {
        return;
    }

    if (outTransferBusy) {
        return;
    }

    if (uxQueueMessagesWaiting(midiOutQueue) == 0) {
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
        return;
    }

    outTransfer->num_bytes = bytesToSend;
    outTransferBusy = true;
    const esp_err_t err = usb_host_transfer_submit(outTransfer);
    if (err != ESP_OK) {
        outTransferBusy = false;
        USB_LOG("[USB] OUT submit failed: %d\n", err);
    }
}

void USBConnection::_onSendComplete(usb_transfer_t* transfer)
{
    USBConnection* usbCon = static_cast<USBConnection*>(transfer->context);
    if (usbCon == nullptr) {
        return;
    }
    usbCon->outTransferBusy = false;
    usbCon->processMidiOutQueue();
}

void USBConnection::_findAndClaimMidiInterface(const usb_intf_desc_t* intf)
{
    if (intf == nullptr || isReady) {
        return;
    }

    const bool isStandardMidi = (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                                 intf->bInterfaceSubClass == 0x03);
    const bool isLegacyAudio = (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                                intf->bInterfaceSubClass == 0x00);
    const bool isVendorMidi = (intf->bInterfaceClass == 0xFF &&
                               intf->bNumEndpoints >= 1);

    if (!isStandardMidi && !isLegacyAudio && !isVendorMidi) {
        return;
    }

    if (isVendorMidi) {
        BRIDGE_LOG_LN("[USB] Warning: Device is in VENDOR mode (Class 0xFF). Roland pianos MUST be in 'Generic' mode for this bridge.");
    }

    BRIDGE_LOG("[USB] Attempting to claim interface %d...\n", intf->bInterfaceNumber);
    const esp_err_t err = usb_host_interface_claim(clientHandle, deviceHandle,
                                                   intf->bInterfaceNumber, intf->bAlternateSetting);
    if (err != ESP_OK) {
        BRIDGE_LOG("[USB] Failed to claim interface %d (err=%d)\n", intf->bInterfaceNumber, err);
        return;
    }

    midiInterfaceNumber = static_cast<int8_t>(intf->bInterfaceNumber);
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

void USBConnection::_setupMidiEndpoint(const usb_ep_desc_t* endpoint)
{
    if (endpoint == nullptr) {
        return;
    }

    if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
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
    isReady = true;

    vTaskDelay(pdMS_TO_TICKS(200));
    usb_host_transfer_submit(midiInTransfers[0]);
    BRIDGE_LOG_LN("[USB] Fallback connected on 0x81.");
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
    bool inClaimedInterface = false;

    BRIDGE_LOG("[USB] Scanning %d bytes of descriptors...\n", totalLength);

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
            BRIDGE_LOG("[USB] Found Interface %d: Class 0x%02X, SubClass 0x%02X, Endpoints %d\n",
                       intf->bInterfaceNumber, intf->bInterfaceClass,
                       intf->bInterfaceSubClass, intf->bNumEndpoints);

            inClaimedInterface = false;
            if (!isReady && midiInterfaceNumber < 0) {
                _findAndClaimMidiInterface(intf);
                inClaimedInterface = (midiInterfaceNumber >= 0);
            } else if (midiInterfaceNumber >= 0 &&
                       intf->bInterfaceNumber == static_cast<uint8_t>(midiInterfaceNumber)) {
                inClaimedInterface = true;
            }
        } else if (descriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && inClaimedInterface) {
            const auto* ep = reinterpret_cast<const usb_ep_desc_t*>(&p[index]);
            _setupMidiEndpoint(ep);
        }

        index += len;
    }

    if (midiInterfaceNumber >= 0 && midiInTransfers[0] != nullptr) {
        isReady = true;
        claimedOk = true;
        BRIDGE_LOG_LN("[USB]   MIDI IN/OUT configured. Applying Casio CT-S1 delay...");
        vTaskDelay(pdMS_TO_TICKS(200));

        for (int i = 0; i < kNumMidiInTransfers; i++) {
            if (midiInTransfers[i] != nullptr) {
                usb_host_transfer_submit(midiInTransfers[i]);
            }
        }
        return;
    }

    if (!claimedOk) {
        claimedOk = _tryEndpointFallback();
    }

    if (!claimedOk && lastError.length() == 0) {
        lastError = "No USB MIDI interface found";
    }
}
