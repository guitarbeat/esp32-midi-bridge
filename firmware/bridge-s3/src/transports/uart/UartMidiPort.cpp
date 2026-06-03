#include "UartMidiPort.h"

UartMidiPort::UartMidiPort(HardwareSerial& serial, int rxPin, int txPin)
    : serial_(&serial), rxPin_(rxPin), txPin_(txPin), parser_(onMidiReceived, this), initialized_(false)
{
}

bool UartMidiPort::begin(uint32_t baud) {
#if CONFIG_IDF_TARGET_ESP32S3
    // GPIO 43/44 are U0TXD/U0RXD for the USB Serial/JTAG controller. Reconfiguring
    // them as UART breaks flashing and serial logging over the native USB port.
    if (rxPin_ == 43 || rxPin_ == 44 || txPin_ == 43 || txPin_ == 44) {
        Serial.println("[UART] ERROR: GPIO 43/44 are USB Serial/JTAG — pick other pins");
        return false;
    }
#endif
    serial_->begin(baud, SERIAL_8N1, rxPin_, txPin_);
    serial_->setTimeout(0); // Non-blocking readBytes
    initialized_ = true;
    return true;
}

bool UartMidiPort::sendMidi(const uint8_t* packet, size_t length) {
    if (!initialized_ || packet == nullptr || length == 0) return false;
    serial_->write(packet, length);
    return true;
}

void UartMidiPort::task() {
    if (!initialized_) return;
    
    uint8_t buffer[64];
    int available = serial_->available();
    if (available > 0) {
        if (available > (int)sizeof(buffer)) available = sizeof(buffer);
        int read = serial_->readBytes(buffer, available);
        parser_.parse(buffer, read);
    }
}

void UartMidiPort::onMidiReceived(uint8_t status, const uint8_t* data, size_t length, size_t sysexPos, void* arg) {
    UartMidiPort* self = static_cast<UartMidiPort*>(arg);
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
