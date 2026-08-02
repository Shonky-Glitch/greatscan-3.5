#pragma once

#include <Arduino.h>

namespace greatscan {

struct TelemetryFrame {
  uint8_t id = 0;
  uint8_t length = 0;
  uint8_t payload[16] = {0};
};

class UartTransport {
 public:
  void begin(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin);
  void tick();
  bool hasFrame() const;
  TelemetryFrame takeFrame();
  bool isHealthy() const;

 private:
  static constexpr size_t kBufferSize = 64;
  static constexpr uint8_t kHeader0 = 0xA5;
  static constexpr uint8_t kHeader1 = 0x5A;

  HardwareSerial* serial_ = nullptr;
  uint8_t rxBuffer_[kBufferSize] = {0};
  size_t rxIndex_ = 0;
  bool frameReady_ = false;
  bool healthy_ = false;
  TelemetryFrame frame_ {};
  unsigned long lastByteMs_ = 0;
  unsigned long lastFrameMs_ = 0;

  bool parseByte(uint8_t byte);
  void resetBuffer();
};

}  // namespace greatscan
