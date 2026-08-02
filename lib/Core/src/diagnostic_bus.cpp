#include "diagnostic_bus.h"

const char* diagnosticBusName(const DiagnosticBus bus)
{
  switch (bus)
  {
    case DiagnosticBus::Can1:
      return "CAN1";
    case DiagnosticBus::Can2:
      return "CAN2";
    default:
      return "none";
  }
}

DiagnosticBus resolveDiagnosticBus(
  const DiagnosticBus selected,
  const BusStatus& can1,
  const BusStatus& can2
)
{
  if (selected == DiagnosticBus::Can1 && can1.started)
  {
    return DiagnosticBus::Can1;
  }

  if (selected == DiagnosticBus::Can2 && can2.started)
  {
    return DiagnosticBus::Can2;
  }

  if (can1.detectedTraffic)
  {
    return DiagnosticBus::Can1;
  }

  if (can2.detectedTraffic)
  {
    return DiagnosticBus::Can2;
  }

  if (can1.started)
  {
    return DiagnosticBus::Can1;
  }

  if (can2.started)
  {
    return DiagnosticBus::Can2;
  }

  return DiagnosticBus::None;
}