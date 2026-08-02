#include "telemetry/telemetry_bridge.h"

namespace greatscan {

GaugePacket buildGaugePacketFromTelemetry(const VehicleTelemetry& telemetry, const uint32_t sequence, const uint32_t timestampMs) {
  GaugePacket packet {};
  packet.protocolVersion = kGaugePacketProtocolVersion;
  packet.sequence = sequence;
  packet.timestampMs = timestampMs;
  packet.engineRpm = telemetry.valid ? static_cast<uint16_t>(telemetry.rpm) : 0;
  packet.vehicleSpeedKphX10 = telemetry.valid ? static_cast<int16_t>(telemetry.speedKph * 10) : 0;
  packet.coolantTempCX10 = telemetry.valid ? static_cast<int16_t>(telemetry.coolantC * 10) : 0;
  packet.transTempCX10 = telemetry.valid ? static_cast<int16_t>(telemetry.transmissionC * 10) : 0;
  packet.oilTempCX10 = telemetry.valid ? static_cast<int16_t>(telemetry.oilTempC * 10) : 0;
  packet.boostKpaX10 = telemetry.valid ? static_cast<int16_t>(telemetry.boostKpa * 10) : 0;
  packet.batteryMv = telemetry.valid ? static_cast<uint16_t>(telemetry.batteryMv) : 0;
  packet.fuelPctX10 = telemetry.valid ? static_cast<int16_t>(telemetry.fuelPct * 10) : kGaugeI16Unavailable;
  packet.throttlePctX10 = telemetry.valid ? static_cast<int16_t>(telemetry.throttlePct * 10) : kGaugeI16Unavailable;
  packet.dpfPctX10 = telemetry.valid ? static_cast<int16_t>(0) : kGaugeI16Unavailable;
  packet.oilPressureKpaX10 = telemetry.valid ? static_cast<int16_t>(telemetry.oilPressureKpa * 10) : kGaugeI16Unavailable;
  packet.afrX100 = telemetry.valid ? static_cast<int16_t>(telemetry.afrX100) : kGaugeI16Unavailable;
  packet.eg3TempCX10 = telemetry.valid ? static_cast<int16_t>(telemetry.eg3C * 10) : kGaugeI16Unavailable;
  packet.o2s1Mv = telemetry.valid ? static_cast<int16_t>(telemetry.o2s1Mv) : kGaugeI16Unavailable;
  packet.engineSpeedDidRpm = telemetry.valid ? static_cast<int16_t>(telemetry.rpm) : kGaugeI16Unavailable;
  packet.statusFlags = WarningNone;
  packet.heartbeatCounter = sequence;
  return packet;
}

}  // namespace greatscan
