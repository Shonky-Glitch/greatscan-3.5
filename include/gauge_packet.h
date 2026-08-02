#pragma once

#include <type_traits>
#include <stdint.h>

namespace greatscan {

constexpr uint8_t kGaugePacketProtocolVersion = 2;
constexpr int16_t kGaugeI16Unavailable = INT16_MIN;

enum class GearState : uint8_t {
  Unknown = 0,
  Park = 1,
  Reverse = 2,
  Neutral = 3,
  Drive = 4,
  Manual = 5,
};

enum WarningFlags : uint32_t {
  WarningNone = 0,
  WarningCanOffline = (1UL << 0),
  WarningEngineHot = (1UL << 1),
  WarningAtfHot = (1UL << 2),
  WarningOilHot = (1UL << 3),
  WarningLowVoltage = (1UL << 4),
  WarningCommStale = (1UL << 5)
};

constexpr uint32_t kGaugeStatusFlagDiscoveryBeacon = (1UL << 31);

#pragma pack(push, 1)
struct GaugePacket {
  uint8_t protocolVersion = kGaugePacketProtocolVersion;
  uint32_t sequence = 0;
  uint32_t timestampMs = 0;
  uint16_t engineRpm = 0;
  int16_t vehicleSpeedKphX10 = 0;
  int16_t coolantTempCX10 = 0;
  int16_t transTempCX10 = 0;
  int16_t oilTempCX10 = 0;
  int16_t boostKpaX10 = 0;
  uint16_t batteryMv = 0;
  uint8_t selectedGear = static_cast<uint8_t>(GearState::Unknown);
  int16_t fuelPctX10 = kGaugeI16Unavailable;
  int16_t throttlePctX10 = kGaugeI16Unavailable;
  int16_t dpfPctX10 = kGaugeI16Unavailable;
  int16_t oilPressureKpaX10 = kGaugeI16Unavailable;
  int16_t afrX100 = kGaugeI16Unavailable;
  int16_t eg3TempCX10 = kGaugeI16Unavailable;
  int16_t o2s1Mv = kGaugeI16Unavailable;
  int16_t engineSpeedDidRpm = kGaugeI16Unavailable;
  uint32_t statusFlags = WarningNone;
  uint32_t heartbeatCounter = 0;
};
#pragma pack(pop)

static_assert(sizeof(GaugePacket) == 48, "GaugePacket must stay 48 bytes");
static_assert(std::is_trivially_copyable<GaugePacket>::value, "GaugePacket must be trivially copyable");

} // namespace greatscan
