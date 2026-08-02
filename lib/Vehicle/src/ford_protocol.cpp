#include "ford_protocol.h"

#include <cstdio>
#include <cstring>

namespace
{
constexpr FordModule FORD_MODULES[] = {
  {0x7E0, 0x7E8, "PCM"},
  {0x7E1, 0x7E9, "TCM"},
  {0x7E2, 0x7EA, "ABS"},
  {0x7E3, 0x7EB, "SRS"},
  {0x7E4, 0x7EC, "BCM"},
  {0x7E5, 0x7ED, "IPC"},
  {0x7E6, 0x7EE, "Gateway"},
  {0x7E7, 0x7EF, "HVAC"},
  {0x7A7, 0x7AF, "ECU 7AF"},
  {0x791, 0x799, "ECU 799"},
  {0x760, 0x768, "ECU 768"},
  {0x751, 0x759, "ECU 759"},
  {0x737, 0x73F, "ECU 73F"},
  {0x730, 0x738, "ECU 738"},
  {0x727, 0x72F, "ECU 72F"},
  {0x724, 0x72C, "ECU 72C"},
  {0x720, 0x728, "IPC (0x728)"},
  {0x716, 0x71E, "ECU 71E"},
  {0x706, 0x70E, "ECU 70E"},
};
}

const char* pidName(const uint8_t pid)
{
  switch (pid)
  {
    case FordPids::SUPPORTED_01_20:
      return "Supported PIDs 01-20";
    case FordPids::SUPPORTED_21_40:
      return "Supported PIDs 21-40";
    case FordPids::SUPPORTED_41_60:
      return "Supported PIDs 41-60";
    case FordPids::SUPPORTED_61_80:
      return "Supported PIDs 61-80";
    case FordPids::ENGINE_LOAD:
      return "Engine Load";
    case FordPids::SHORT_TERM_FUEL_TRIM_B1:
      return "STFT B1";
    case FordPids::LONG_TERM_FUEL_TRIM_B1:
      return "LTFT B1";
    case FordPids::FUEL_PRESSURE:
      return "Fuel Pressure";
    case FordPids::INTAKE_MAP:
      return "Intake MAP";
    case FordPids::ENGINE_SPEED:
      return "Engine RPM";
    case FordPids::VEHICLE_SPEED:
      return "Vehicle Speed";
    case FordPids::TIMING_ADVANCE:
      return "Timing Advance";
    case FordPids::INTAKE_AIR_TEMP:
      return "Intake Air Temp";
    case FordPids::MAF_RATE:
      return "MAF";
    case FordPids::COOLANT_TEMP:
      return "Coolant Temp";
    case FordPids::O2_SENSOR_1:
      return "O2 Sensor 1";
    case FordPids::RUN_TIME_SINCE_START:
      return "Run Time";
    case FordPids::FUEL_LEVEL:
      return "Fuel Level";
    case FordPids::DISTANCE_SINCE_DTC_CLEAR:
      return "Distance Since DTC Clear";
    case FordPids::CONTROL_MODULE_VOLTAGE:
      return "Control Module Voltage";
    case FordPids::AMBIENT_AIR_TEMP:
      return "Ambient Air Temp";
    case FordPids::ENGINE_OIL_TEMP:
      return "Engine Oil Temp";
    case FordPids::THROTTLE_POSITION:
      return "Throttle";
    default:
      return "Unknown PID";
  }
}

const char* infoTypeName(const uint8_t infoType)
{
  switch (infoType)
  {
    case 0x01:
      return "VIN";
    case 0x02:
      return "Calibration ID";
    case 0x03:
      return "CVN";
    case 0x0A:
      return "ECU Name";
    default:
      return "Vehicle Info";
  }
}

const char* nrcName(const uint8_t nrc)
{
  switch (nrc)
  {
    case 0x11:
      return "Service not supported";
    case 0x12:
      return "Subfunction not supported";
    case 0x13:
      return "Invalid format";
    case 0x21:
      return "Busy, repeat request";
    case 0x22:
      return "Conditions not correct";
    case 0x31:
      return "Request out of range";
    case 0x33:
      return "Security access denied";
    case 0x78:
      return "Response pending";
    default:
      return "Unknown NRC";
  }
}

bool isFordResponse(const uint32_t canId)
{
  return fordModuleIndexByResponseId(canId) >= 0;
}

bool formatPidValue(
  const uint8_t pid,
  const uint8_t* value,
  const size_t valueLength,
  char* output,
  const size_t outputSize
)
{
  if (!output || outputSize == 0)
  {
    return false;
  }

  output[0] = '\0';

  switch (pid)
  {
    case FordPids::SUPPORTED_01_20:
    case FordPids::SUPPORTED_21_40:
    case FordPids::SUPPORTED_41_60:
    case FordPids::SUPPORTED_61_80:
      if (valueLength >= 4)
      {
        std::snprintf(output, outputSize, "%s: %02X %02X %02X %02X", pidName(pid), value[0], value[1], value[2], value[3]);
        return true;
      }
      break;
    case FordPids::ENGINE_LOAD:
      if (valueLength >= 1)
      {
        const float pct = static_cast<float>(value[0]) * 100.0f / 255.0f;
        std::snprintf(output, outputSize, "%s: %.1f%%", pidName(pid), pct);
        return true;
      }
      break;
    case FordPids::SHORT_TERM_FUEL_TRIM_B1:
    case FordPids::LONG_TERM_FUEL_TRIM_B1:
      if (valueLength >= 1)
      {
        const float pct = (static_cast<int>(value[0]) - 128) * 100.0f / 128.0f;
        std::snprintf(output, outputSize, "%s: %.1f%%", pidName(pid), pct);
        return true;
      }
      break;
    case FordPids::FUEL_PRESSURE:
      if (valueLength >= 1)
      {
        const unsigned int kpa = static_cast<unsigned int>(value[0]) * 3U;
        std::snprintf(output, outputSize, "%s: %u kPa", pidName(pid), kpa);
        return true;
      }
      break;
    case FordPids::INTAKE_MAP:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %u kPa", pidName(pid), value[0]);
        return true;
      }
      break;
    case FordPids::ENGINE_SPEED:
      if (valueLength >= 2)
      {
        const uint16_t rpm = (static_cast<uint16_t>(value[0]) << 8 | value[1]) / 4;
        std::snprintf(output, outputSize, "%s: %u RPM", pidName(pid), rpm);
        return true;
      }
      break;
    case FordPids::VEHICLE_SPEED:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %u km/h", pidName(pid), value[0]);
        return true;
      }
      break;
    case FordPids::TIMING_ADVANCE:
      if (valueLength >= 1)
      {
        const float deg = static_cast<float>(value[0]) / 2.0f - 64.0f;
        std::snprintf(output, outputSize, "%s: %.1f deg", pidName(pid), deg);
        return true;
      }
      break;
    case FordPids::INTAKE_AIR_TEMP:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %d C", pidName(pid), static_cast<int>(value[0]) - 40);
        return true;
      }
      break;
    case FordPids::MAF_RATE:
      if (valueLength >= 2)
      {
        const float maf = (static_cast<uint16_t>(value[0]) << 8 | value[1]) / 100.0f;
        std::snprintf(output, outputSize, "%s: %.2f g/s", pidName(pid), maf);
        return true;
      }
      break;
    case FordPids::COOLANT_TEMP:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %d C", pidName(pid), static_cast<int>(value[0]) - 40);
        return true;
      }
      break;
    case FordPids::O2_SENSOR_1:
      if (valueLength >= 1)
      {
        const float volts = static_cast<float>(value[0]) / 200.0f;
        std::snprintf(output, outputSize, "%s: %.3f V", pidName(pid), volts);
        return true;
      }
      break;
    case FordPids::RUN_TIME_SINCE_START:
      if (valueLength >= 2)
      {
        const uint16_t seconds = static_cast<uint16_t>(value[0]) << 8 | value[1];
        std::snprintf(output, outputSize, "%s: %u s", pidName(pid), seconds);
        return true;
      }
      break;
    case FordPids::FUEL_LEVEL:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %u%%", pidName(pid), static_cast<unsigned>((value[0] * 100U) / 255U));
        return true;
      }
      break;
    case FordPids::DISTANCE_SINCE_DTC_CLEAR:
      if (valueLength >= 2)
      {
        const uint16_t km = static_cast<uint16_t>(value[0]) << 8 | value[1];
        std::snprintf(output, outputSize, "%s: %u km", pidName(pid), km);
        return true;
      }
      break;
    case FordPids::CONTROL_MODULE_VOLTAGE:
      if (valueLength >= 2)
      {
        const float volts = (static_cast<uint16_t>(value[0]) << 8 | value[1]) / 1000.0f;
        std::snprintf(output, outputSize, "%s: %.3f V", pidName(pid), volts);
        return true;
      }
      break;
    case FordPids::AMBIENT_AIR_TEMP:
    case FordPids::ENGINE_OIL_TEMP:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %d C", pidName(pid), static_cast<int>(value[0]) - 40);
        return true;
      }
      break;
    case FordPids::THROTTLE_POSITION:
      if (valueLength >= 1)
      {
        std::snprintf(output, outputSize, "%s: %u%%", pidName(pid), static_cast<unsigned>((value[0] * 100U) / 255U));
        return true;
      }
      break;
    default:
      break;
  }

  return false;
}

bool formatVehicleInfoValue(
  const uint8_t infoType,
  const uint8_t* value,
  const size_t valueLength,
  char* output,
  const size_t outputSize
)
{
  if (!output || outputSize == 0)
  {
    return false;
  }

  output[0] = '\0';

  if (infoType == 0x01)
  {
    char vin[18] = {};
    const size_t copyLength = valueLength < sizeof(vin) - 1 ? valueLength : sizeof(vin) - 1;
    std::memcpy(vin, value, copyLength);
    std::snprintf(output, outputSize, "VIN: %s", vin);
    return true;
  }

  size_t written = std::snprintf(output, outputSize, "%s:", infoTypeName(infoType));
  for (size_t index = 0; index < valueLength && written + 4 < outputSize; ++index)
  {
    written += std::snprintf(output + written, outputSize - written, " %02X", value[index]);
  }
  return true;
}

bool decodeDtcBytes(
  const uint8_t first,
  const uint8_t second,
  char* output,
  const size_t outputSize
)
{
  if (!output || outputSize < 6 || (first == 0 && second == 0))
  {
    return false;
  }

  static const char dtcFamily[] = {'P', 'C', 'B', 'U'};
  const char family = dtcFamily[(first >> 6) & 0x03];
  const uint8_t digit1 = (first >> 4) & 0x03;
  const uint8_t digit2 = first & 0x0F;
  const uint8_t digit3 = (second >> 4) & 0x0F;
  const uint8_t digit4 = second & 0x0F;
  std::snprintf(output, outputSize, "%c%u%X%X%X", family, digit1, digit2, digit3, digit4);
  return true;
}

bool decodeUdsDtcBytes(
  const uint8_t first,
  const uint8_t second,
  const uint8_t third,
  char* output,
  const size_t outputSize
)
{
  if (!output || outputSize < 7 || (first == 0 && second == 0 && third == 0))
  {
    return false;
  }

  std::snprintf(output, outputSize, "%02X%02X%02X", first, second, third);
  return true;
}

size_t fordModuleCount()
{
  return sizeof(FORD_MODULES) / sizeof(FORD_MODULES[0]);
}

const FordModule* fordModuleByIndex(const size_t index)
{
  if (index >= fordModuleCount())
  {
    return nullptr;
  }

  return &FORD_MODULES[index];
}

const char* fordModuleNameByResponseId(const uint32_t responseId)
{
  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (module && module->responseId == responseId)
    {
      return module->name;
    }
  }

  return "Unknown Module";
}

int fordModuleIndexByResponseId(const uint32_t responseId)
{
  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (module && module->responseId == responseId)
    {
      return static_cast<int>(index);
    }
  }

  return -1;
}