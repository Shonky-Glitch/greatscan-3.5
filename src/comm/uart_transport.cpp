#include "comm/uart_transport.h"

namespace greatscan {
namespace {
constexpr uint8_t kFrameTypeAck = 0x01;
constexpr uint8_t kFrameTypeTelemetry = 0x02;
}

void UartTransport::begin(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin) {
  serial_ = &serial;
  serial.begin(baud, SERIAL_8N1, rxPin, txPin);
  serial.setRxBufferSize(256);
  rxIndex_ = 0;
  frameReady_ = false;
  healthy_ = true;
  lastByteMs_ = millis();
  lastFrameMs_ = millis();
}

void UartTransport::tick() {
  if (serial_ == nullptr) {
    healthy_ = false;
    return;
  }

  while (serial_->available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(serial_->read());
    if (parseByte(byte)) {
      lastFrameMs_ = millis();
    }
    lastByteMs_ = millis();
  }

  if (millis() - lastByteMs_ > 1000U) {
    healthy_ = false;
  }
}

bool UartTransport::hasFrame() const {
  return frameReady_;
}

TelemetryFrame UartTransport::takeFrame() {
  TelemetryFrame result = frame_;
  frameReady_ = false;
  return result;
}

bool UartTransport::isHealthy() const {
  return healthy_;
}

bool UartTransport::parseByte(uint8_t byte) {
  if (rxIndex_ == 0 && byte != kHeader0) {
    return false;
  }
  if (rxIndex_ == 1 && byte != kHeader1) {
    rxIndex_ = 0;
    return false;
  }
  if (rxIndex_ == 2) {
    frame_.id = byte;
  } else if (rxIndex_ == 3) {
    frame_.length = byte;
    if (frame_.length > sizeof(frame_.payload)) {
      resetBuffer();
      return false;
    }
  } else if (rxIndex_ >= 4 && rxIndex_ < static_cast<size_t>(4 + frame_.length)) {
    frame_.payload[rxIndex_ - 4] = byte;
  } else if (rxIndex_ == static_cast<size_t>(4 + frame_.length)) {
    const uint8_t checksum = byte;
    (void)checksum;
    frameReady_ = true;
    healthy_ = true;
    resetBuffer();
    return true;
  }

  rxIndex_++;
  if (rxIndex_ > kBufferSize) {
    resetBuffer();
  }
  return false;
}

void UartTransport::resetBuffer() {
  rxIndex_ = 0;
  frame_.id = 0;
  frame_.length = 0;
  memset(frame_.payload, 0, sizeof(frame_.payload));
}

}  // namespace greatscan
