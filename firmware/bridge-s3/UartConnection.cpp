#include "UartConnection.h"

UartConnection::UartConnection(HardwareSerial& serial, int rxPin, int txPin)
    : serial_(&serial), rxPin_(rxPin), txPin_(txPin), parser_(onMidiReceived, this), initialized_(false)
{
}

bool UartConnection::begin(uint32_t baud) {
    serial_->begin(baud, SERIAL_8N1, rxPin_, txPin_);
    serial_->setTimeout(0); // Non-blocking readBytes
    initialized_ = true;
    return true;
}

bool UartConnection::sendMidi(const uint8_t* packet, size_t length) {
    if (!initialized_ || packet == nullptr || length == 0) return false;
    serial_->write(packet, length);
    return true;
}

void UartConnection::task() {
    if (!initialized_) return;
    
    uint8_t buffer[64];
    int available = serial_->available();
    if (available > 0) {
        if (available > (int)sizeof(buffer)) available = sizeof(buffer);
        int read = serial_->readBytes(buffer, available);
        parser_.parse(buffer, read);
    }
}

void UartConnection::onMidiReceived(uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg) {
    UartConnection* self = static_cast<UartConnection*>(arg);
    if (!self || !self->receiveCallback_) return;

    // Pack into a temporary buffer to present a complete message chunk to the bridge
    uint8_t packet[256]; 
    if (length + 1 > sizeof(packet)) return;

    packet[0] = status;
    if (length > 0 && data != nullptr) {
        memcpy(packet + 1, data, length);
    }
    
    self->receiveCallback_(packet, 1 + length);
}
