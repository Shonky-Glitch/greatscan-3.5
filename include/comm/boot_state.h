#pragma once

#include <Arduino.h>

namespace greatscan {

class BootState {
 public:
  enum class Phase : uint8_t {
    Booting = 0,
    TransportInit,
    Ready,
    Fault
  };

  void begin();
  void markTransportHealthy(bool healthy);
  void markUiReady(bool ready);
  Phase phase() const;
  const char* phaseName() const;
  unsigned long bootMs() const;

 private:
  Phase phase_ = Phase::Booting;
  bool transportHealthy_ = false;
  bool uiReady_ = false;
  unsigned long startedMs_ = 0;
};

}  // namespace greatscan
