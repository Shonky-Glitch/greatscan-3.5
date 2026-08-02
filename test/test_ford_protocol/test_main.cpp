#include <cstring>

#include <ford_protocol.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>
#define CHECK_TRUE TEST_ASSERT_TRUE
#define CHECK_FALSE TEST_ASSERT_FALSE
#define CHECK_EQUAL_STRING TEST_ASSERT_EQUAL_STRING
#else
#include <cassert>
#define CHECK_TRUE assert
#define CHECK_FALSE(value) assert(!(value))
#define CHECK_EQUAL_STRING(expected, actual) assert(std::strcmp((expected), (actual)) == 0)
#endif

void runFordProtocolChecks()
{
  char output[64] = {};
  const uint8_t rpmValue[] = {0x1A, 0xF8};
  const uint8_t o2Value[] = {0x64};
  CHECK_TRUE(isFordResponse(0x7E8));
  CHECK_TRUE(isFordResponse(0x72F));
  CHECK_FALSE(isFordResponse(0x123));
  CHECK_EQUAL_STRING("Engine RPM", pidName(FordPids::ENGINE_SPEED));
  CHECK_TRUE(formatPidValue(FordPids::ENGINE_SPEED, rpmValue, sizeof(rpmValue), output, sizeof(output)));
  CHECK_EQUAL_STRING("Engine RPM: 1726 RPM", output);
  CHECK_EQUAL_STRING("O2 Sensor 1", pidName(FordPids::O2_SENSOR_1));
  CHECK_TRUE(formatPidValue(FordPids::O2_SENSOR_1, o2Value, sizeof(o2Value), output, sizeof(output)));
  CHECK_EQUAL_STRING("O2 Sensor 1: 0.500 V", output);

  const uint8_t vin[] = {'M','P','2','C','A','N','T','E','S','T','1','2','3','4','5','6','7'};
  CHECK_TRUE(formatVehicleInfoValue(0x01, vin, sizeof(vin), output, sizeof(output)));
  CHECK_EQUAL_STRING("VIN: MP2CANTEST1234567", output);

  CHECK_TRUE(decodeDtcBytes(0x01, 0x23, output, sizeof(output)));
  CHECK_EQUAL_STRING("P0123", output);

  CHECK_TRUE(decodeUdsDtcBytes(0x91, 0x6A, 0x11, output, sizeof(output)));
  CHECK_EQUAL_STRING("916A11", output);
  CHECK_EQUAL_STRING("Request out of range", nrcName(0x31));
}

#ifdef ARDUINO
void test_ford_protocol()
{
  runFordProtocolChecks();
}

void setup()
{
  delay(1000);
  UNITY_BEGIN();
  RUN_TEST(test_ford_protocol);
  UNITY_END();
}

void loop()
{
}
#else
int main()
{
  runFordProtocolChecks();
  return 0;
}
#endif