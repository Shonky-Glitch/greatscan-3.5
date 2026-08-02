#include <cstring>

#include "telemetry/vehicle_data.h"
#include "telemetry/telemetry_bridge.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>

void test_buildGaugePacketFromTelemetry() {
  greatscan::VehicleTelemetry telemetry {};
  telemetry.valid = true;
  telemetry.rpm = 4200;
  telemetry.speedKph = 95;
  telemetry.coolantC = 88;
  telemetry.oilTempC = 98;
  telemetry.fuelPct = 63;
  telemetry.throttlePct = 55;
  telemetry.batteryMv = 12800;
  telemetry.transmissionC = 72;
  telemetry.boostKpa = 140;
  telemetry.oilPressureKpa = 180;
  telemetry.afrX100 = 1470;
  telemetry.atfC = 74;
  telemetry.eg3C = 108;
  telemetry.o2s1Mv = 750;

  const greatscan::GaugePacket packet = greatscan::buildGaugePacketFromTelemetry(telemetry, 42, 1234);

  TEST_ASSERT_EQUAL_UINT32(42u, packet.sequence);
  TEST_ASSERT_EQUAL_UINT32(1234u, packet.timestampMs);
  TEST_ASSERT_EQUAL_UINT16(4200u, packet.engineRpm);
  TEST_ASSERT_EQUAL_INT16(950, packet.vehicleSpeedKphX10);
  TEST_ASSERT_EQUAL_INT16(880, packet.coolantTempCX10);
  TEST_ASSERT_EQUAL_INT16(720, packet.transTempCX10);
  TEST_ASSERT_EQUAL_INT16(980, packet.oilTempCX10);
  TEST_ASSERT_EQUAL_INT16(630, packet.fuelPctX10);
  TEST_ASSERT_EQUAL_INT16(550, packet.throttlePctX10);
  TEST_ASSERT_EQUAL_UINT16(12800u, packet.batteryMv);
  TEST_ASSERT_EQUAL_INT16(1400, packet.boostKpaX10);
  TEST_ASSERT_EQUAL_INT16(1800, packet.oilPressureKpaX10);
  TEST_ASSERT_EQUAL_INT16(1470, packet.afrX100);
  TEST_ASSERT_EQUAL_INT16(740, packet.eg3TempCX10);
  TEST_ASSERT_EQUAL_INT16(750, packet.o2s1Mv);
  TEST_ASSERT_EQUAL_UINT32(greatscan::WarningNone, packet.statusFlags);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_buildGaugePacketFromTelemetry);
  UNITY_END();
}

void loop() {}

#else
#include <cassert>

int main() {
  greatscan::VehicleTelemetry telemetry {};
  telemetry.valid = true;
  telemetry.rpm = 4200;
  telemetry.speedKph = 95;
  telemetry.coolantC = 88;
  telemetry.oilTempC = 98;
  telemetry.fuelPct = 63;
  telemetry.throttlePct = 55;
  telemetry.batteryMv = 12800;
  telemetry.transmissionC = 72;
  telemetry.boostKpa = 140;
  telemetry.oilPressureKpa = 180;
  telemetry.afrX100 = 1470;
  telemetry.atfC = 74;
  telemetry.eg3C = 108;
  telemetry.o2s1Mv = 750;

  const greatscan::GaugePacket packet = greatscan::buildGaugePacketFromTelemetry(telemetry, 42, 1234);
  assert(packet.sequence == 42u);
  assert(packet.timestampMs == 1234u);
  assert(packet.engineRpm == 4200u);
  assert(packet.vehicleSpeedKphX10 == 950);
  assert(packet.coolantTempCX10 == 880);
  assert(packet.transTempCX10 == 720);
  assert(packet.oilTempCX10 == 980);
  assert(packet.fuelPctX10 == 630);
  assert(packet.throttlePctX10 == 550);
  assert(packet.batteryMv == 12800u);
  assert(packet.boostKpaX10 == 1400);
  assert(packet.oilPressureKpaX10 == 1800);
  assert(packet.afrX100 == 1470);
  assert(packet.eg3TempCX10 == 740);
  assert(packet.o2s1Mv == 750);
  assert(packet.statusFlags == greatscan::WarningNone);
  return 0;
}
#endif
