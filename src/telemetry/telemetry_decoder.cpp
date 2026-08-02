#include "telemetry/telemetry_decoder.h"

#include <cstring>

namespace greatscan {
namespace {
constexpr uint8_t kFrameTypeTelemetry = 0x02;
constexpr uint8_t kFrameTypeAck = 0x01;
}

void TelemetryDecoder::begin() {
  latest_ = {};
  latest_.vehicleName[0] = 'A';
  latest_.vehicleName[1] = 'U';
  latest_.vehicleName[2] = 'T';
  latest_.vehicleName[3] = 'O';
  latest_.vehicleName[4] = '\0';
  hasNewData_ = false;
}

void TelemetryDecoder::update(const TelemetryFrame& frame) {
  hasNewData_ = false;
  if (frame.id != kFrameTypeTelemetry || frame.length < 2) {
    return;
  }

  latest_.valid = true;
  latest_.ignitionOn = (frame.payload[0] & 0x01U) != 0;
  latest_.rpm = static_cast<uint16_t>((frame.payload[1] << 8) | frame.payload[2]);
  latest_.speedKph = static_cast<uint16_t>((frame.payload[3] << 8) | frame.payload[4]);
  latest_.coolantC = static_cast<int16_t>((frame.payload[5] << 8) | frame.payload[6]);
  latest_.oilTempC = static_cast<int16_t>((frame.payload[7] << 8) | frame.payload[8]);
  latest_.fuelPct = static_cast<int16_t>((frame.payload[9] << 8) | frame.payload[10]);
  latest_.throttlePct = static_cast<int16_t>((frame.payload[11] << 8) | frame.payload[12]);
  latest_.batteryMv = static_cast<int16_t>((frame.payload[13] << 8) | frame.payload[14]);
  latest_.transmissionC = static_cast<int16_t>((frame.payload[15] << 8) | frame.payload[16]);
  latest_.boostKpa = static_cast<int16_t>((frame.payload[17] << 8) | frame.payload[18]);
  latest_.oilPressureKpa = static_cast<int16_t>((frame.payload[17] << 8) | frame.payload[18]);
  latest_.afrX100 = static_cast<int16_t>((frame.payload[19] << 8) | frame.payload[20]);
  latest_.atfC = static_cast<int16_t>((frame.payload[21] << 8) | frame.payload[22]);
  latest_.eg3C = static_cast<int16_t>((frame.payload[23] << 8) | frame.payload[24]);
  latest_.o2s1Mv = static_cast<int16_t>((frame.payload[25] << 8) | frame.payload[26]);
  latest_.faultCount = frame.payload[27];
  std::strncpy(latest_.vehicleName, "FORD", sizeof(latest_.vehicleName) - 1);
  latest_.vehicleName[sizeof(latest_.vehicleName) - 1] = '\0';
  hasNewData_ = true;
}

const VehicleTelemetry& TelemetryDecoder::latest() const {
  return latest_;
}

bool TelemetryDecoder::hasNewData() const {
  return hasNewData_;
}

}  // namespace greatscan
