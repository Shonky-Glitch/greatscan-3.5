#include <ui_state.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>
#define CHECK_TRUE TEST_ASSERT_TRUE
#define CHECK_EQUAL TEST_ASSERT_EQUAL
#define CHECK_EQUAL_STR TEST_ASSERT_EQUAL_STRING
#else
#include <cassert>
#include <cstring>
#define CHECK_TRUE assert
#define CHECK_EQUAL assert
#define CHECK_EQUAL_STR(a, b) assert(std::strcmp((a), (b)) == 0)
#endif

void runUiStateChecks()
{
  UiState state {};
  uiStateInit(state);
  CHECK_EQUAL(state.diagnosticBus == DiagnosticBus::None, true);
  CHECK_EQUAL(state.currentPage == UiPage::Overview, true);
  CHECK_EQUAL_STR(state.vin, "unknown");

  BusStatus can1 {};
  can1.started = true;
  BusStatus can2 {};
  uiStateSetBusStatus(state, can1, "500 kbit/s", can2, "offline");
  uiStateSetBus(state, DiagnosticBus::Can1);
  uiStateNextPage(state);
  uiStatePreviousPage(state);
  uiStateSetPage(state, UiPage::Faults);
  uiStateSetVin(state, "MP2TESTVIN1234567");
  uiStateSetDtcCount(state, 2);
  uiStateSetLatestDtc(state, "P0123");
  uiStateSetPidValue(state, 0x0C, "Engine RPM", "Engine RPM: 850 RPM");
  uiStateSetStatus(state, "diag ready");

  CHECK_EQUAL(state.can1Started, true);
  CHECK_EQUAL_STR(state.can1Bitrate, "500 kbit/s");
  CHECK_EQUAL(state.diagnosticBus == DiagnosticBus::Can1, true);
  CHECK_EQUAL(state.currentPage == UiPage::Faults, true);
  CHECK_EQUAL_STR(uiPageName(state.currentPage), "Faults");
  CHECK_EQUAL_STR(state.vin, "MP2TESTVIN1234567");
  CHECK_EQUAL(state.dtcCount, 2);
  CHECK_EQUAL_STR(state.lastDtc, "P0123");
  CHECK_EQUAL(state.pids[0].valid, true);
  CHECK_EQUAL_STR(state.pids[0].label, "Engine RPM");
  CHECK_EQUAL_STR(state.status, "diag ready");

  uiStateMarkClean(state);
  CHECK_EQUAL(state.dirty, false);
}

#ifdef ARDUINO
void test_ui_state()
{
  runUiStateChecks();
}

void setup()
{
  delay(1000);
  UNITY_BEGIN();
  RUN_TEST(test_ui_state);
  UNITY_END();
}

void loop()
{
}
#else
int main()
{
  runUiStateChecks();
  return 0;
}
#endif