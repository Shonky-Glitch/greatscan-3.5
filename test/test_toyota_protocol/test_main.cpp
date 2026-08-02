#include <cstring>

#include <toyota_protocol.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>
#define CHECK_TRUE TEST_ASSERT_TRUE
#define CHECK_FALSE TEST_ASSERT_FALSE
#define CHECK_EQUAL TEST_ASSERT_EQUAL
#define CHECK_EQUAL_STRING TEST_ASSERT_EQUAL_STRING
#else
#include <cassert>
#define CHECK_TRUE assert
#define CHECK_FALSE(value) assert(!(value))
#define CHECK_EQUAL(expected, actual) assert((expected) == (actual))
#define CHECK_EQUAL_STRING(expected, actual) assert(std::strcmp((expected), (actual)) == 0)
#endif

void runToyotaProtocolChecks()
{
  CHECK_TRUE(isToyotaResponse(0x7E8));
  CHECK_FALSE(isToyotaResponse(0x123));
  CHECK_EQUAL(8U, toyotaModuleCount());

  const ToyotaModule* module0 = toyotaModuleByIndex(0);
  CHECK_TRUE(module0 != nullptr);
  CHECK_EQUAL(0x7E0U, module0->requestId);
  CHECK_EQUAL(0x7E8U, module0->responseId);
  CHECK_EQUAL_STRING("Engine/ECM", module0->name);

  CHECK_EQUAL_STRING("Transmission", toyotaModuleNameByResponseId(0x7E9));
  CHECK_EQUAL(2, toyotaModuleIndexByResponseId(0x7EA));
  CHECK_EQUAL(-1, toyotaModuleIndexByResponseId(0x123));
}

#ifdef ARDUINO
void test_toyota_protocol()
{
  runToyotaProtocolChecks();
}

void setup()
{
  delay(1000);
  UNITY_BEGIN();
  RUN_TEST(test_toyota_protocol);
  UNITY_END();
}

void loop()
{
}
#else
int main()
{
  runToyotaProtocolChecks();
  return 0;
}
#endif
