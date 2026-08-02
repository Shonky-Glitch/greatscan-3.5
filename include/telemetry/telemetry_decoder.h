#pragma once

#include <Arduino.h>

#include "comm/uart_transport.h"
#include "telemetry/vehicle_data.h"

namespace greatscan {

class TelemetryDecoder {
 public:
  void begin();
  void update(const TelemetryFrame& frame);
  const VehicleTelemetry& latest() const;
  bool hasNewData() const;

 private:
  VehicleTelemetry latest_ {};
  bool hasNewData_ = false;
};

}  // namespace greatscan
