#include <diagnostic_bus.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>
#define CHECK_TRUE TEST_ASSERT_TRUE
#define CHECK_EQUAL TEST_ASSERT_EQUAL
#else
#include <cassert>
#define CHECK_TRUE assert
#define CHECK_EQUAL assert
#endif

void runDiagnosticBusChecks()
{
  const BusStatus idle {};
  BusStatus can1Started {};
  can1Started.started = true;
  BusStatus can2Started {};
  can2Started.started = true;
  BusStatus can1Traffic {};
  can1Traffic.started = true;
  can1Traffic.detectedTraffic = true;
  BusStatus can2Traffic {};
  can2Traffic.started = true;
  can2Traffic.detectedTraffic = true;

  CHECK_EQUAL(resolveDiagnosticBus(DiagnosticBus::None, idle, idle) == DiagnosticBus::None, true);
  CHECK_EQUAL(resolveDiagnosticBus(DiagnosticBus::None, can1Started, idle) == DiagnosticBus::Can1, true);
  CHECK_EQUAL(resolveDiagnosticBus(DiagnosticBus::None, idle, can2Started) == DiagnosticBus::Can2, true);
  CHECK_EQUAL(resolveDiagnosticBus(DiagnosticBus::Can1, can1Started, can2Traffic) == DiagnosticBus::Can1, true);
  CHECK_EQUAL(resolveDiagnosticBus(DiagnosticBus::None, can1Traffic, can2Traffic) == DiagnosticBus::Can1, true);
  CHECK_TRUE(diagnosticBusName(DiagnosticBus::Can2)[0] == 'C');
}

#ifdef ARDUINO
void test_diagnostic_bus()
{
  runDiagnosticBusChecks();
}

void setup()
{
  delay(1000);
  UNITY_BEGIN();
  RUN_TEST(test_diagnostic_bus);
  UNITY_END();
}

void loop()
{
}
#else
int main()
{
  runDiagnosticBusChecks();
  return 0;
}
#endif