#include "comm/boot_state.h"

namespace greatscan {

void BootState::begin() {
  startedMs_ = millis();
  phase_ = Phase::Booting;
  transportHealthy_ = false;
  uiReady_ = false;
}

void BootState::markTransportHealthy(bool healthy) {
  transportHealthy_ = healthy;
  if (phase_ == Phase::Booting && healthy) {
    phase_ = Phase::TransportInit;
  }
}

void BootState::markUiReady(bool ready) {
  uiReady_ = ready;
  if (phase_ == Phase::TransportInit && transportHealthy_ && ready) {
    phase_ = Phase::Ready;
  } else if (phase_ == Phase::Booting && ready) {
    phase_ = Phase::TransportInit;
  }
}

BootState::Phase BootState::phase() const {
  return phase_;
}

const char* BootState::phaseName() const {
  switch (phase_) {
    case Phase::Booting:
      return "BOOTING";
    case Phase::TransportInit:
      return "TRANSPORT_INIT";
    case Phase::Ready:
      return "READY";
    case Phase::Fault:
    default:
      return "FAULT";
  }
}

unsigned long BootState::bootMs() const {
  return millis() - startedMs_;
}

}  // namespace greatscan
