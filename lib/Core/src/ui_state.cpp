#include "ui_state.h"

#include <cstring>

namespace
{
void copyText(char* destination, const size_t destinationSize, const char* source)
{
  if (!destination || destinationSize == 0)
  {
    return;
  }

  const char* safeSource = source ? source : "";
  std::strncpy(destination, safeSource, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

bool textChanged(const char* destination, const char* source)
{
  const char* safeSource = source ? source : "";
  return std::strcmp(destination, safeSource) != 0;
}
}

void uiStateInit(UiState& state)
{
  state = {};
  copyText(state.can1Bitrate, sizeof(state.can1Bitrate), "offline");
  copyText(state.can2Bitrate, sizeof(state.can2Bitrate), "offline");
  copyText(state.vin, sizeof(state.vin), "unknown");
  copyText(state.lastDtc, sizeof(state.lastDtc), "none");
  copyText(state.status, sizeof(state.status), "booting");
  state.dirty = true;
}

const char* uiPageName(const UiPage page)
{
  switch (page)
  {
    case UiPage::Overview:
      return "Overview";
    case UiPage::LivePids:
      return "Live PIDs";
    case UiPage::VehicleInfo:
      return "Vehicle Info";
    case UiPage::Faults:
      return "Faults";
    case UiPage::Help:
      return "Help";
    default:
      return "Unknown";
  }
}

void uiStateSetBus(UiState& state, const DiagnosticBus bus)
{
  if (state.diagnosticBus != bus)
  {
    state.diagnosticBus = bus;
    state.dirty = true;
  }
}

void uiStateSetPage(UiState& state, const UiPage page)
{
  if (state.currentPage != page)
  {
    state.currentPage = page;
    state.dirty = true;
  }
}

void uiStateNextPage(UiState& state)
{
  const auto page = static_cast<uint8_t>(state.currentPage);
  uiStateSetPage(state, static_cast<UiPage>((page + 1) % 5));
}

void uiStatePreviousPage(UiState& state)
{
  const auto page = static_cast<uint8_t>(state.currentPage);
  uiStateSetPage(state, static_cast<UiPage>((page + 4) % 5));
}

void uiStateSetBusStatus(
  UiState& state,
  const BusStatus& can1,
  const char* can1Bitrate,
  const BusStatus& can2,
  const char* can2Bitrate
)
{
  if (state.can1Started != can1.started || state.can2Started != can2.started)
  {
    state.can1Started = can1.started;
    state.can2Started = can2.started;
    state.dirty = true;
  }

  if (textChanged(state.can1Bitrate, can1Bitrate))
  {
    copyText(state.can1Bitrate, sizeof(state.can1Bitrate), can1Bitrate);
    state.dirty = true;
  }

  if (textChanged(state.can2Bitrate, can2Bitrate))
  {
    copyText(state.can2Bitrate, sizeof(state.can2Bitrate), can2Bitrate);
    state.dirty = true;
  }
}

void uiStateSetVin(UiState& state, const char* vin)
{
  if (textChanged(state.vin, vin))
  {
    copyText(state.vin, sizeof(state.vin), vin);
    state.dirty = true;
  }
}

void uiStateSetDtcCount(UiState& state, const uint8_t dtcCount)
{
  if (state.dtcCount != dtcCount)
  {
    state.dtcCount = dtcCount;
    state.dirty = true;
  }
}

void uiStateSetLatestDtc(UiState& state, const char* dtcCode)
{
  if (textChanged(state.lastDtc, dtcCode))
  {
    copyText(state.lastDtc, sizeof(state.lastDtc), dtcCode);
    state.dirty = true;
  }
}

void uiStateSetPidValue(UiState& state, const uint8_t pid, const char* label, const char* value)
{
  size_t slot = UI_PID_SLOTS;
  for (size_t index = 0; index < UI_PID_SLOTS; ++index)
  {
    if (state.pids[index].valid && state.pids[index].pid == pid)
    {
      slot = index;
      break;
    }
    if (slot == UI_PID_SLOTS && !state.pids[index].valid)
    {
      slot = index;
    }
  }

  if (slot == UI_PID_SLOTS)
  {
    slot = 0;
  }

  UiPidEntry& entry = state.pids[slot];
  entry.pid = pid;
  entry.valid = true;
  copyText(entry.label, sizeof(entry.label), label);
  copyText(entry.value, sizeof(entry.value), value);
  state.dirty = true;
}

void uiStateSetStatus(UiState& state, const char* status)
{
  if (textChanged(state.status, status))
  {
    copyText(state.status, sizeof(state.status), status);
    state.dirty = true;
  }
}

void uiStateMarkClean(UiState& state)
{
  state.dirty = false;
}