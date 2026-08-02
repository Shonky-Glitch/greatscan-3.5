#pragma once

#include <cstdint>

enum class DiagnosticBus : uint8_t
{
  None = 0,
  Can1 = 1,
  Can2 = 2,
};

struct BusStatus
{
  bool started = false;
  bool detectedTraffic = false;
  unsigned long lastFrameMs = 0;
};

const char* diagnosticBusName(DiagnosticBus bus);
DiagnosticBus resolveDiagnosticBus(DiagnosticBus selected, const BusStatus& can1, const BusStatus& can2);