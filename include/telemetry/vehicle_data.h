#pragma once

#include <Arduino.h>

namespace greatscan {

struct VehicleTelemetry {
  bool valid = false;
  bool ignitionOn = false;
  uint16_t rpm = 0;
  uint16_t speedKph = 0;
  int16_t coolantC = 0;
  int16_t oilTempC = 0;
  int16_t fuelPct = 0;
  int16_t throttlePct = 0;
  int16_t batteryMv = 0;
  int16_t transmissionC = 0;
  int16_t boostKpa = 0;
  int16_t oilPressureKpa = 0;
  int16_t afrX100 = 0;
  int16_t atfC = 0;
  int16_t eg3C = 0;
  int16_t o2s1Mv = 0;
  uint8_t faultCount = 0;
  char vehicleName[16] = "AUTO";
};

}  // namespace greatscan
