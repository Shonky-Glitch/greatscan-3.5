#include <isotp_session.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>
#define CHECK_TRUE TEST_ASSERT_TRUE
#define CHECK_EQUAL TEST_ASSERT_EQUAL
#else
#include <cassert>
#define CHECK_TRUE assert
#define CHECK_EQUAL(expected, actual) assert((expected) == (actual))
#endif

void runIsoTpChecks()
{
  IsoTpSession session {};
  const uint8_t firstFrame[8] = {0x10, 0x13, 0x49, 0x01, 'M', 'P', '2', 'C'};
  const uint8_t secondFrame[8] = {0x21, 'A', 'N', 'T', 'E', 'S', 'T', '1'};
  const uint8_t thirdFrame[8] = {0x22, '2', '3', '4', '5', '6', '7', 0x00};

  CHECK_EQUAL(IsoTpUpdate::Started, handleIsoTpFirstFrame(session, 0x7E8, firstFrame, sizeof(firstFrame)));
  CHECK_TRUE(session.active);
  CHECK_EQUAL(0x49, session.service);
  CHECK_EQUAL(0x01, session.infoType);
  CHECK_EQUAL(IsoTpUpdate::Continued, handleIsoTpConsecutiveFrame(session, 0x7E8, secondFrame, sizeof(secondFrame)));
  CHECK_EQUAL(IsoTpUpdate::Complete, handleIsoTpConsecutiveFrame(session, 0x7E8, thirdFrame, sizeof(thirdFrame)));
  CHECK_EQUAL(17, session.bytesCollected);
  CHECK_EQUAL('M', session.buffer[0]);
  CHECK_EQUAL('7', session.buffer[16]);
}

#ifdef ARDUINO
void test_isotp_session()
{
  runIsoTpChecks();
}

void setup()
{
  delay(1000);
  UNITY_BEGIN();
  RUN_TEST(test_isotp_session);
  UNITY_END();
}

void loop()
{
}
#else
int main()
{
  runIsoTpChecks();
  return 0;
}
#endif