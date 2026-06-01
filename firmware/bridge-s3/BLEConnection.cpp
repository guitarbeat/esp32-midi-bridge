#include "BLEConnection.h"
#include <BLE2902.h>

#include "BridgeLog.h"
#include "MidiCodec.h"

void BLEConnection::processIncomingBlePacket(const uint8_t* data, size_t length)
{
    if (data == nullptr || length < 3 || !(data[0] & 0x80)) {
        return;
    }

    size_t i = 1;
    while (i < length) {
        if (i + 1 < length && (data[i] & 0x80) && (data[i + 1] & 0x80)) {
            i++;
        }

        if (i >= length) {
            break;
        }

        const uint8_t status = data[i];
        if (!(status & 0x80)) {
            i++;
            continue;
        }

        const uint8_t msgLen = MidiCodec::lengthFromStatus(status);
        if (msgLen == 0 || i + msgLen > length) {
            break;
        }

        enqueueIncomingMidi(data + i, msgLen);
        i += msgLen;
    }
}

BLEConnection::BLEConnection()
    : pServer(nullptr),
      pCharacteristic(nullptr),
      pBleCallback(nullptr),
      pServerCallback(nullptr),
      sendMutex(nullptr),
      midiCallback(nullptr),
      avgLatencyMs_(0),
      subscribed_(false),
      incomingHead_(0),
      incomingTail_(0),
      incomingMux_(portMUX_INITIALIZER_UNLOCKED)
{
}

BLEConnection::~BLEConnection() {
    if (sendMutex) {
        vSemaphoreDelete(sendMutex);
        sendMutex = nullptr;
    }
    delete pBleCallback;
    pBleCallback = nullptr;
    delete pServerCallback;
    pServerCallback = nullptr;
}

void BLEConnection::begin(const std::string& deviceName) {
    if (pServer) {
        return;
    }

    sendMutex = xSemaphoreCreateMutex();
    BLEDevice::init(String(deviceName.c_str()));
    pServer = BLEDevice::createServer();

    class ServerCallbacks : public BLEServerCallbacks {
    public:
        BLEConnection* bleCon;
        explicit ServerCallbacks(BLEConnection* con) : bleCon(con) {}

        void onConnect(BLEServer* server) override {
            BLEDevice::setPower(ESP_PWR_LVL_P9);
            server->updateConnParams(server->getConnId(), 6, 12, 0, 400);
            bleCon->setSubscribed(false);
            BRIDGE_LOG_LN("[BLE] Central connected; waiting for MIDI notify subscription");
        }

        void onDisconnect(BLEServer*) override {
            bleCon->setSubscribed(false);
            BRIDGE_LOG_LN("[BLE] Central disconnected; advertising restarted");
            BLEDevice::startAdvertising();
        }
    };
    delete pServerCallback;
    pServerCallback = new ServerCallbacks(this);
    pServer->setCallbacks(pServerCallback);

    BLEService* pService = pServer->createService(BLE_MIDI_SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        BLE_MIDI_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->addDescriptor(new BLE2902());

    class BLECallback : public BLECharacteristicCallbacks {
    public:
        BLEConnection* bleCon;
        BLECallback(BLEConnection* con) : bleCon(con) {}
        void onWrite(BLECharacteristic* characteristic) override {
            auto rxValue = characteristic->getValue();
            if (rxValue.length() >= 2) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(rxValue.c_str());
                bleCon->processIncomingBlePacket(data, rxValue.length());
            }
        }

#if defined(CONFIG_NIMBLE_ENABLED)
        void onSubscribe(BLECharacteristic*, ble_gap_conn_desc*, uint16_t subValue) override {
            bleCon->setSubscribed(subValue > 0);
            BRIDGE_LOG("[BLE] MIDI notify subscription %s (value=%u)\n",
                       subValue > 0 ? "enabled" : "disabled",
                       subValue);
        }
#endif
    };

    delete pBleCallback;
    pBleCallback = new BLECallback(this);
    pCharacteristic->setCallbacks(pBleCallback);
    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_MIDI_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

bool BLEConnection::sendMidi(const uint8_t* data, size_t length) {
    if (!pCharacteristic || !sendMutex || length == 0) return false;

    const size_t midiLen = (length > 18) ? 18 : length;

    if (xSemaphoreTake(sendMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    bool sent = false;
    if (isSubscribed()) {
        uint8_t blePacket[256];
        size_t outLen = 0;

        if (MidiCodec::buildBlePacket(data, midiLen, millis(), blePacket, &outLen)) {
            pCharacteristic->setValue(blePacket, outLen);
            pCharacteristic->notify();
            sent = true;
        }
    }

    xSemaphoreGive(sendMutex);
    return sent;
}

void BLEConnection::recordForwardLatency(uint32_t latencyMs)
{
    if (avgLatencyMs_ == 0) {
        avgLatencyMs_ = static_cast<uint16_t>(latencyMs);
    } else {
        avgLatencyMs_ = static_cast<uint16_t>((avgLatencyMs_ * 3 + latencyMs) / 4);
    }
}

void BLEConnection::task()
{
    RawBleMessage message;
    while (dequeueIncomingMidi(message)) {
        dispatchIncomingMidi(message.data, message.length);
    }
}

bool BLEConnection::isConnected() const {
    if(pServer)
        return (pServer->getConnectedCount() > 0);
    return false;
}

void BLEConnection::setMidiMessageCallback(MIDIMessageCallback cb) {
    midiCallback = cb;
}

void BLEConnection::onMidiDataReceived(const uint8_t* data, size_t length) {
    if (receiveCallback_) {
        receiveCallback_(data, length);
    }
}

bool BLEConnection::enqueueIncomingMidi(const uint8_t* data, size_t length)
{
    if (data == nullptr || length == 0 || length > sizeof(incomingQueue_[0].data)) {
        return false;
    }

    portENTER_CRITICAL(&incomingMux_);
    const uint8_t next = (incomingHead_ + 1) % kIncomingQueueDepth;
    if (next == incomingTail_) {
        portEXIT_CRITICAL(&incomingMux_);
        return false;
    }

    RawBleMessage& message = incomingQueue_[incomingHead_];
    memcpy(message.data, data, length);
    message.length = length;
    incomingHead_ = next;
    portEXIT_CRITICAL(&incomingMux_);
    return true;
}

bool BLEConnection::dequeueIncomingMidi(RawBleMessage& message)
{
    portENTER_CRITICAL(&incomingMux_);
    if (incomingTail_ == incomingHead_) {
        portEXIT_CRITICAL(&incomingMux_);
        return false;
    }

    message = incomingQueue_[incomingTail_];
    incomingTail_ = (incomingTail_ + 1) % kIncomingQueueDepth;
    portEXIT_CRITICAL(&incomingMux_);
    return true;
}

void BLEConnection::dispatchIncomingMidi(const uint8_t* data, size_t length)
{
    if (midiCallback) {
        midiCallback(data, length);
    }
    onMidiDataReceived(data, length);
}

void BLEConnection::setSubscribed(bool subscribed)
{
    subscribed_ = subscribed;
}
