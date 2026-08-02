#pragma once

#include <cstddef>
#include <cstdint>

#include "diagnostic_bus.h"

constexpr size_t UI_TEXT_SIZE = 96;
constexpr size_t UI_PID_SLOTS = 5;

enum class UiPage : uint8_t
{
  Overview = 0,
  LivePids,
  VehicleInfo,
  Faults,
  Help,
};

struct UiPidEntry
{
  uint8_t pid = 0;
  bool valid = false;
  char label[24] = {};
  char value[48] = {};
};

struct UiState
{
  DiagnosticBus diagnosticBus = DiagnosticBus::None;
  UiPage currentPage = UiPage::Overview;
  bool can1Started = false;
  bool can2Started = false;
  char can1Bitrate[24] = "offline";
  char can2Bitrate[24] = "offline";
  char vin[24] = "unknown";
  uint8_t dtcCount = 0;
  char lastDtc[12] = "none";
  char status[UI_TEXT_SIZE] = "booting";
  UiPidEntry pids[UI_PID_SLOTS] = {};
  bool dirty = true;
};

void uiStateInit(UiState& state);
void uiStateSetBus(UiState& state, DiagnosticBus bus);
void uiStateSetPage(UiState& state, UiPage page);
void uiStateNextPage(UiState& state);
void uiStatePreviousPage(UiState& state);
const char* uiPageName(UiPage page);
void uiStateSetBusStatus(UiState& state, const BusStatus& can1, const char* can1Bitrate, const BusStatus& can2, const char* can2Bitrate);
void uiStateSetVin(UiState& state, const char* vin);
void uiStateSetDtcCount(UiState& state, uint8_t dtcCount);
void uiStateSetLatestDtc(UiState& state, const char* dtcCode);
void uiStateSetPidValue(UiState& state, uint8_t pid, const char* label, const char* value);
void uiStateSetStatus(UiState& state, const char* status);
void uiStateMarkClean(UiState& state);