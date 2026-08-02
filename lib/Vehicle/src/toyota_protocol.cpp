#include "toyota_protocol.h"

namespace
{
constexpr ToyotaModule TOYOTA_MODULES[] = {
  {0x7E0, 0x7E8, "Engine/ECM"},
  {0x7E1, 0x7E9, "Transmission"},
  {0x7E2, 0x7EA, "ABS/VSC"},
  {0x7E3, 0x7EB, "SRS Airbag"},
  {0x7E4, 0x7EC, "Body/BCM"},
  {0x7E5, 0x7ED, "Cluster"},
  {0x7E6, 0x7EE, "Gateway/Smart Key"},
  {0x7E7, 0x7EF, "HVAC"},
};
}

bool isToyotaResponse(const uint32_t canId)
{
  return canId >= 0x7E8 && canId <= 0x7EF;
}

size_t toyotaModuleCount()
{
  return sizeof(TOYOTA_MODULES) / sizeof(TOYOTA_MODULES[0]);
}

const ToyotaModule* toyotaModuleByIndex(const size_t index)
{
  if (index >= toyotaModuleCount())
  {
    return nullptr;
  }

  return &TOYOTA_MODULES[index];
}

const char* toyotaModuleNameByResponseId(const uint32_t responseId)
{
  for (size_t index = 0; index < toyotaModuleCount(); ++index)
  {
    const ToyotaModule* module = toyotaModuleByIndex(index);
    if (module && module->responseId == responseId)
    {
      return module->name;
    }
  }

  return "Unknown Module";
}

int toyotaModuleIndexByResponseId(const uint32_t responseId)
{
  for (size_t index = 0; index < toyotaModuleCount(); ++index)
  {
    const ToyotaModule* module = toyotaModuleByIndex(index);
    if (module && module->responseId == responseId)
    {
      return static_cast<int>(index);
    }
  }

  return -1;
}
