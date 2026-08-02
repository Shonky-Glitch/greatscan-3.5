#pragma once

#include <cstddef>
#include <cstdint>

struct ToyotaModule
{
  uint32_t requestId;
  uint32_t responseId;
  const char* name;
};

namespace ToyotaServices
{
constexpr uint8_t CLEAR_DTC = 0x04;
constexpr uint8_t CLEAR_DTC_RESPONSE = 0x44;
constexpr uint8_t NEGATIVE_RESPONSE = 0x7F;
}

bool isToyotaResponse(uint32_t canId);
size_t toyotaModuleCount();
const ToyotaModule* toyotaModuleByIndex(size_t index);
const char* toyotaModuleNameByResponseId(uint32_t responseId);
int toyotaModuleIndexByResponseId(uint32_t responseId);
