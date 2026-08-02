#include "scanner_runtime.h"

#include <Arduino.h>
#include <SPI.h>
#include <driver/twai.h>
#include <mcp2515.h>

#include <gauge_packet.h>

#include <diagnostic_bus.h>
#include <ford_protocol.h>
#include <isotp_session.h>
#include <ui_state.h>

#include "Display/waveshare_ui.h"
#include "Network/espnow_manager.h"

namespace Pins
{
#ifdef STATUS_LED_PIN
constexpr gpio_num_t STATUS_LED = static_cast<gpio_num_t>(STATUS_LED_PIN);
#else
constexpr gpio_num_t STATUS_LED = GPIO_NUM_NC;
#endif
constexpr gpio_num_t CAN1_RX = GPIO_NUM_6;
constexpr gpio_num_t CAN1_TX = GPIO_NUM_7;
constexpr int CAN2_CS = 10;
constexpr int CAN2_CLK = 12;
constexpr int CAN2_MISO = 13;
constexpr int CAN2_MOSI = 11;
constexpr gpio_num_t CAN2_IRQ = GPIO_NUM_3;
}

struct CanBitrateOption
{
  twai_timing_config_t timing;
  CAN_SPEED mcpSpeed;
  uint32_t bitrate;
  const char* label;
};

struct RuntimeBusState
{
  BusStatus status;
  uint32_t activeBitrate = 0;
  const char* activeLabel = "offline";
};

struct RuntimeState
{
  RuntimeBusState can1;
  RuntimeBusState can2;
  DiagnosticBus diagnosticBus = DiagnosticBus::None;
  unsigned long lastPidMillis = 0;
  unsigned long lastHeartbeatMillis = 0;
  unsigned long lastStatusMillis = 0;
  unsigned long lastVehicleInfoMillis = 0;
  unsigned long lastDtcMillis = 0;
  unsigned long fordSweepStartedMillis = 0;
  unsigned long fordDiagSweepStartedMillis = 0;
  unsigned long clearWorkflowStartedMillis = 0;
  uint32_t fordModuleMask = 0;
  uint32_t clearAttemptMask = 0;
  uint32_t clearAckMask = 0;
  uint8_t clearAttemptCount = 0;
  uint8_t clearAckCount = 0;
  uint8_t clearWorkflowStage = 0;
  bool clearWorkflowActive = false;
  size_t nextPidIndex = 0;
  size_t nextCriticalPidIndex = 0;
  uint8_t pidScheduleTick = 0;
  size_t nextCustomDidIndex = 0;
};

struct LivePidSnapshot
{
  bool loadValid = false;
  float loadPct = 0.0f;
  bool rpmValid = false;
  float rpm = 0.0f;
  bool speedValid = false;
  float speedKph = 0.0f;
  bool coolantValid = false;
  float coolantC = 0.0f;
  bool stftB1Valid = false;
  float stftB1Pct = 0.0f;
  bool ltftB1Valid = false;
  float ltftB1Pct = 0.0f;
  bool fuelPressureValid = false;
  float fuelPressureKpa = 0.0f;
  bool intakeMapValid = false;
  float intakeMapKpa = 0.0f;
  bool boostValid = false;
  float boostKpa = 0.0f;
  bool timingValid = false;
  float timingAdvanceDeg = 0.0f;
  bool intakeAirValid = false;
  float intakeAirC = 0.0f;
  bool mafValid = false;
  float mafGps = 0.0f;
  bool runtimeValid = false;
  float runtimeSeconds = 0.0f;
  bool fuelValid = false;
  float fuelPct = 0.0f;
  bool distanceValid = false;
  float distanceKm = 0.0f;
  bool voltageValid = false;
  float voltage = 0.0f;
  bool ambientValid = false;
  float ambientC = 0.0f;
  bool oilTempValid = false;
  float oilTempC = 0.0f;
  bool throttleValid = false;
  float throttlePct = 0.0f;
  bool dpfValid = false;
  float dpfPct = 0.0f;
  bool o2s1Valid = false;
  float o2s1V = 0.0f;
  bool atfValid = false;
  float atfC = 0.0f;
  bool eg3Valid = false;
  float eg3C = 0.0f;
  bool didEngineRpmValid = false;
  float didEngineRpm = 0.0f;
  bool ecuSerialValid = false;
  char ecuSerialHex[40] = {};
};

struct FordCustomDid
{
  uint32_t requestId;
  uint16_t did;
  const char* name;
};

struct DiagnosticSnapshot
{
  uint8_t storedCount = 0;
  uint8_t pendingCount = 0;
  uint8_t permanentCount = 0;
  char lastStored[8] = "none";
  char lastPending[8] = "none";
  char lastPermanent[8] = "none";
};

struct ModuleDtcSnapshot
{
  bool seen = false;
  uint8_t storedCount = 0;
  uint8_t pendingCount = 0;
  uint8_t permanentCount = 0;
  char lastStored[8] = "none";
  char lastPending[8] = "none";
  char lastPermanent[8] = "none";
};

struct PidSupportState
{
  bool discovered = false;
  bool supported[256] = {};
};

constexpr CanBitrateOption BITRATES[] = {
  {TWAI_TIMING_CONFIG_500KBITS(), CAN_500KBPS, 500000, "500 kbit/s"},
  {TWAI_TIMING_CONFIG_250KBITS(), CAN_250KBPS, 250000, "250 kbit/s"},
  {TWAI_TIMING_CONFIG_125KBITS(), CAN_125KBPS, 125000, "125 kbit/s"},
  {TWAI_TIMING_CONFIG_1MBITS(), CAN_1000KBPS, 1000000, "1 Mbit/s"},
};

constexpr uint8_t PID_SEQUENCE[] = {
  FordPids::ENGINE_LOAD,
  FordPids::SHORT_TERM_FUEL_TRIM_B1,
  FordPids::LONG_TERM_FUEL_TRIM_B1,
  FordPids::FUEL_PRESSURE,
  FordPids::INTAKE_MAP,
  FordPids::ENGINE_SPEED,
  FordPids::VEHICLE_SPEED,
  FordPids::TIMING_ADVANCE,
  FordPids::INTAKE_AIR_TEMP,
  FordPids::MAF_RATE,
  FordPids::O2_SENSOR_1,
  FordPids::COOLANT_TEMP,
  FordPids::RUN_TIME_SINCE_START,
  FordPids::FUEL_LEVEL,
  FordPids::DISTANCE_SINCE_DTC_CLEAR,
  FordPids::CONTROL_MODULE_VOLTAGE,
  FordPids::AMBIENT_AIR_TEMP,
  FordPids::ENGINE_OIL_TEMP,
  FordPids::THROTTLE_POSITION,
};

// The fastest-changing gauge values (RPM, vehicle speed, boost/MAP) get a
// dedicated round-robin slot every scheduler tick, with exactly one slow
// tick per full pass through this list (see sendNextPid()). With a 3-item
// list that means each of RPM/speed/boost is requested every 4 ticks --
// at PID_INTERVAL_MS=100 that is a ~400ms refresh instead of the previous
// ~1s. Battery voltage moved out of this list (it's slow-changing and not
// gauge-critical) and is now covered, along with coolant/throttle/fuel/
// oil-temp/etc., by the remaining 1-in-4 tick via the full PID_SEQUENCE list.
constexpr uint8_t CRITICAL_PID_SEQUENCE[] = {
  FordPids::ENGINE_SPEED,
  FordPids::VEHICLE_SPEED,
  FordPids::INTAKE_MAP,
};

constexpr unsigned long PID_INTERVAL_MS = 100;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 4000;
constexpr unsigned long STATUS_INTERVAL_MS = 3000;
constexpr unsigned long VEHICLE_INFO_INTERVAL_MS = 30000;
constexpr unsigned long DTC_INTERVAL_MS = 45000;

constexpr uint8_t FORD_SVC_READ_DID = 0x22;
constexpr uint8_t FORD_SVC_READ_DID_RESPONSE = 0x62;
constexpr uint16_t FORD_DID_ATF = 0x1E1C;
constexpr uint16_t FORD_DID_EG3 = 0x229F;
constexpr uint16_t FORD_DID_ECU_SERIAL = 0xF192;
constexpr uint16_t FORD_DID_ENGINE_SPEED = 0xF1A0;

constexpr FordCustomDid CUSTOM_DIDS[] = {
  {0x7E1, FORD_DID_ATF, "ATF"},
  {0x7E0, FORD_DID_EG3, "EG3"},
  {0x7E0, FORD_DID_ECU_SERIAL, "ECU-SN"},
  {0x7E0, FORD_DID_ENGINE_SPEED, "RPM-DID"},
};

RuntimeState state;
LivePidSnapshot livePids;
PidSupportState pidSupport;
DiagnosticSnapshot diagnostics;
ModuleDtcSnapshot moduleDiagnostics[32];
MCP2515 can2Controller(Pins::CAN2_CS, 8000000);
IsoTpSession can1IsoTp;
IsoTpSession can2IsoTp;
UiState uiState;
DisplayManager displayManager;
DisplayFrame displayFrame;
bool waveshareUiEnabled = false;
constexpr bool ESP_NOW_TELEMETRY_ENABLED = true;
constexpr bool GSERIAL_BRIDGE_ENABLED = false;
constexpr uint32_t GSERIAL_BRIDGE_BAUD = 115200;
constexpr int GSERIAL_BRIDGE_RX_PIN = 44;
constexpr int GSERIAL_BRIDGE_TX_PIN = 43;
constexpr unsigned long GSERIAL_BRIDGE_INTERVAL_MS = 90;

constexpr uint8_t ESP_NOW_DISPLAY_PEER_MAC[6] = {0xA4, 0xCB, 0x8F, 0xDB, 0x2F, 0xFC};
constexpr uint8_t ESP_NOW_CHANNEL = 1;

HardwareSerial gscanBridgeSerial(1);
char latestRawLine[192] = "boot";
unsigned long latestRawSeq = 0;
unsigned long lastBridgePublishMs = 0;
String bridgeCommandBuffer;
unsigned long bridgeStatusPublishCount = 0;
greatscan::ESPNowManager espNowManager;

void markRawLine(const char* line)
{
  if (line == nullptr)
  {
    return;
  }

  snprintf(latestRawLine, sizeof(latestRawLine), "%s", line);
  ++latestRawSeq;
}

bool hasStatusLed()
{
  return Pins::STATUS_LED != GPIO_NUM_NC;
}

void selectDiagnosticBus(DiagnosticBus bus);
DiagnosticBus resolveBus();
bool sendHeartbeat();
bool sendPidRequest(uint8_t pid);
bool sendCustomDidRequest(uint32_t requestId, uint16_t did);
bool sendVehicleInfoRequest(uint8_t infoType);
bool sendReadDtcRequest();
bool sendReadStoredDtcRequest();
bool sendReadPendingDtcRequest();
bool sendReadPermanentDtcRequest();
bool sendClearDtcRequest();
bool runSafeClearDtcWorkflow();
void requestFordModuleAvailability();
void requestFordModuleDiagnostics();
void printFordModuleAvailability();
void printFordModuleDiagnosticSummary();
void printClearWorkflowSummary();
void sendNextPid();
void sendNextCustomDid();
bool updateCustomDidSnapshot(uint32_t canId, uint16_t did, const uint8_t* value, size_t valueLength);
void parseDidResponsePayload(uint32_t canId, const uint8_t* payload, size_t payloadLength);
void runDiagnosticsFunction();
void requestFullDiagnosticSweep();
void handleSerialCommand(char command);
String buildStatusJson();
void publishGscanBridgeStatus();
void pollGscanBridgeCommands();
void updateEspNowVehicleCache();
void buildDisplayFrame(DisplayFrame& frame);
DisplayPage toDisplayPage(UiPage page);

int16_t clampToI16(const float value, const float scale = 1.0f)
{
  const float scaledFloat = value * scale;
  const long scaled = static_cast<long>(scaledFloat + ((scaledFloat >= 0.0f) ? 0.5f : -0.5f));
  if (scaled > 32767L)
  {
    return 32767;
  }
  if (scaled < -32768L)
  {
    return -32768;
  }
  return static_cast<int16_t>(scaled);
}

uint16_t clampToU16(const float value, const float scale = 1.0f)
{
  const float scaledFloat = value * scale;
  const long scaled = static_cast<long>(scaledFloat + ((scaledFloat >= 0.0f) ? 0.5f : -0.5f));
  if (scaled < 0L)
  {
    return 0;
  }
  if (scaled > 65535L)
  {
    return 65535;
  }
  return static_cast<uint16_t>(scaled);
}

void copyDiagCode(char* dest, const size_t destSize, const char* src)
{
  if (!dest || destSize == 0)
  {
    return;
  }
  const char* safe = src ? src : "none";
  snprintf(dest, destSize, "%s", safe);
}

void updateFordModuleUdsSnapshot(const int moduleIndex, const uint8_t count, const char* lastCode)
{
  const size_t maxModules = sizeof(moduleDiagnostics) / sizeof(moduleDiagnostics[0]);
  if (moduleIndex < 0 || static_cast<size_t>(moduleIndex) >= maxModules)
  {
    return;
  }

  ModuleDtcSnapshot& snapshot = moduleDiagnostics[moduleIndex];
  snapshot.seen = true;
  snapshot.storedCount = count;
  copyDiagCode(snapshot.lastStored, sizeof(snapshot.lastStored), lastCode);
}

void decodeObdDtcPayload(
  const uint8_t* value,
  const size_t valueLength,
  uint8_t& countOut,
  char* lastCodeOut,
  const size_t lastCodeOutSize
)
{
  countOut = 0;
  copyDiagCode(lastCodeOut, lastCodeOutSize, "none");
  if (!value || valueLength < 2)
  {
    return;
  }

  char latest[8] = "none";
  for (size_t index = 0; index + 1 < valueLength; index += 2)
  {
    char code[8] = {};
    if (decodeDtcBytes(value[index], value[index + 1], code, sizeof(code)))
    {
      ++countOut;
      copyDiagCode(latest, sizeof(latest), code);
    }
  }

  copyDiagCode(lastCodeOut, lastCodeOutSize, latest);
}

void resetFordModuleDiagnosticSummary()
{
  const size_t maxModules = sizeof(moduleDiagnostics) / sizeof(moduleDiagnostics[0]);
  for (size_t index = 0; index < maxModules; ++index)
  {
    moduleDiagnostics[index].seen = false;
    moduleDiagnostics[index].storedCount = 0;
    moduleDiagnostics[index].pendingCount = 0;
    moduleDiagnostics[index].permanentCount = 0;
    copyDiagCode(moduleDiagnostics[index].lastStored, sizeof(moduleDiagnostics[index].lastStored), "none");
    copyDiagCode(moduleDiagnostics[index].lastPending, sizeof(moduleDiagnostics[index].lastPending), "none");
    copyDiagCode(moduleDiagnostics[index].lastPermanent, sizeof(moduleDiagnostics[index].lastPermanent), "none");
  }
}

void updateFordModuleDtcSnapshot(
  const int moduleIndex,
  const uint8_t service,
  const uint8_t* value,
  const size_t valueLength
)
{
  const size_t maxModules = sizeof(moduleDiagnostics) / sizeof(moduleDiagnostics[0]);
  if (moduleIndex < 0 || static_cast<size_t>(moduleIndex) >= maxModules)
  {
    return;
  }

  ModuleDtcSnapshot& snapshot = moduleDiagnostics[moduleIndex];
  snapshot.seen = true;
  if (service == FordServices::STORED_DTC_RESPONSE)
  {
    decodeObdDtcPayload(value, valueLength, snapshot.storedCount, snapshot.lastStored, sizeof(snapshot.lastStored));
  }
  else if (service == FordServices::PENDING_DTC_RESPONSE)
  {
    decodeObdDtcPayload(value, valueLength, snapshot.pendingCount, snapshot.lastPending, sizeof(snapshot.lastPending));
  }
  else if (service == FordServices::PERMANENT_DTC_RESPONSE)
  {
    decodeObdDtcPayload(value, valueLength, snapshot.permanentCount, snapshot.lastPermanent, sizeof(snapshot.lastPermanent));
  }
}

void printObdDtcPayload(
  const char* tag,
  const uint8_t* value,
  const size_t valueLength,
  uint8_t& countOut,
  char* lastCodeOut,
  const size_t lastCodeOutSize
)
{
  if (!value || valueLength < 2)
  {
    Serial.printf("[FORD] %s response too short\n", tag);
    return;
  }

  decodeObdDtcPayload(value, valueLength, countOut, lastCodeOut, lastCodeOutSize);

  for (size_t index = 0; index + 1 < valueLength; index += 2)
  {
    char code[8] = {};
    if (decodeDtcBytes(value[index], value[index + 1], code, sizeof(code)))
    {
      Serial.printf("[FORD] %s DTC: %s\n", tag, code);
    }
  }

  Serial.printf("[FORD] %s DTC count: %u\n", tag, countOut);
}

void parseUdsDtcRecords(
  const uint8_t* data,
  const size_t dataLength,
  uint8_t& countOut,
  char* lastCodeOut,
  const size_t lastCodeOutSize
)
{
  countOut = 0;
  copyDiagCode(lastCodeOut, lastCodeOutSize, "none");
  if (!data || dataLength < 4)
  {
    return;
  }

  for (size_t index = 0; index + 3 < dataLength; index += 4)
  {
    char code[12] = {};
    if (!decodeUdsDtcBytes(data[index], data[index + 1], data[index + 2], code, sizeof(code)))
    {
      continue;
    }

    ++countOut;
    copyDiagCode(lastCodeOut, lastCodeOutSize, code);
  }
}

void printUdsDtcValue(const char* moduleName, const int moduleIndex, const uint8_t* value, const size_t valueLength)
{
  if (!value || valueLength < 2)
  {
    Serial.println("[FORD] UDS DTC response too short");
    return;
  }

  const uint8_t subFunction = value[0];
  const uint8_t statusAvailabilityMask = value[1];
  char latestCode[12] = "none";
  uint8_t count = 0;

  if (subFunction == 0x02 || subFunction == 0x04)
  {
    parseUdsDtcRecords(&value[2], valueLength - 2, count, latestCode, sizeof(latestCode));
    Serial.printf(
      "[FORD] UDS 0x19 0x%02X from %s status-mask=0x%02X dtcs=%u latest=%s\n",
      subFunction,
      moduleName,
      statusAvailabilityMask,
      count,
      latestCode
    );
    char statusLine[UI_TEXT_SIZE] = {};
    std::snprintf(
      statusLine,
      sizeof(statusLine),
      "%s 19-%02X dtc:%u %s",
      moduleName,
      subFunction,
      count,
      latestCode
    );
    uiStateSetStatus(uiState, statusLine);
    uiStateSetDtcCount(uiState, count);
    if (count > 0)
    {
      uiStateSetLatestDtc(uiState, latestCode);
    }
    updateFordModuleUdsSnapshot(moduleIndex, count, latestCode);
    return;
  }

  Serial.printf("[FORD] UDS DTC subfunction 0x%02X from %s raw:", subFunction, moduleName);
  for (size_t index = 1; index < valueLength; ++index)
  {
    Serial.printf(" %02X", value[index]);
  }
  Serial.println();
}

bool isSupportBitmapPid(const uint8_t pid)
{
  return pid == FordPids::SUPPORTED_01_20 ||
         pid == FordPids::SUPPORTED_21_40 ||
         pid == FordPids::SUPPORTED_41_60 ||
         pid == FordPids::SUPPORTED_61_80;
}

void updatePidSupportMap(const uint8_t bitmapPid, const uint8_t* value, const size_t valueLength)
{
  if (!isSupportBitmapPid(bitmapPid) || value == nullptr || valueLength < 4)
  {
    return;
  }

  const uint32_t bitmap =
    (static_cast<uint32_t>(value[0]) << 24) |
    (static_cast<uint32_t>(value[1]) << 16) |
    (static_cast<uint32_t>(value[2]) << 8) |
    static_cast<uint32_t>(value[3]);

  for (uint8_t bit = 0; bit < 32; ++bit)
  {
    const uint8_t pid = static_cast<uint8_t>(bitmapPid + 1 + bit);
    const bool supported = ((bitmap >> (31 - bit)) & 0x1U) != 0;
    pidSupport.supported[pid] = supported;
  }

  pidSupport.discovered = true;
}

bool shouldPollPid(const uint8_t pid)
{
  if (isSupportBitmapPid(pid))
  {
    return false;
  }

  if (!pidSupport.discovered)
  {
    return true;
  }

  return pidSupport.supported[pid];
}

void requestSupportedPidMaps()
{
  sendPidRequest(FordPids::SUPPORTED_01_20);
  sendPidRequest(FordPids::SUPPORTED_21_40);
  sendPidRequest(FordPids::SUPPORTED_41_60);
  sendPidRequest(FordPids::SUPPORTED_61_80);
}

void sendNextCustomDid()
{
  constexpr size_t kDidCount = sizeof(CUSTOM_DIDS) / sizeof(CUSTOM_DIDS[0]);
  const FordCustomDid& item = CUSTOM_DIDS[state.nextCustomDidIndex % kDidCount];
  state.nextCustomDidIndex = (state.nextCustomDidIndex + 1U) % kDidCount;
  Serial.printf("[SCAN] Requesting %s DID 0x%04X via 0x%03lX\n", item.name, item.did, static_cast<unsigned long>(item.requestId));
  sendCustomDidRequest(item.requestId, item.did);
}

bool updateCustomDidSnapshot(const uint32_t canId, const uint16_t did, const uint8_t* value, const size_t valueLength)
{
  if (!value || valueLength < 1)
  {
    return false;
  }

  if (did == FORD_DID_ATF && canId == 0x7E9)
  {
    // ScanGauge MTH 003F000A0000 => raw * 6.3
    livePids.atfC = static_cast<float>(value[0]) * 6.3f;
    livePids.atfValid = true;
    Serial.printf("[PID] ATF: %.1f C\n", livePids.atfC);
    return true;
  }

  if (did == FORD_DID_EG3 && canId == 0x7E8)
  {
    // ScanGauge MTH 000500010000 => raw * 5
    livePids.eg3C = static_cast<float>(value[0]) * 5.0f;
    livePids.eg3Valid = true;
    Serial.printf("[PID] EG3: %.1f C\n", livePids.eg3C);
    return true;
  }

  if (did == FORD_DID_ECU_SERIAL)
  {
    const size_t bytesToFormat = valueLength < 8 ? valueLength : 8;
    size_t offset = 0;
    livePids.ecuSerialHex[0] = '\0';
    for (size_t i = 0; i < bytesToFormat && offset + 3 < sizeof(livePids.ecuSerialHex); ++i)
    {
      const int written = std::snprintf(
        livePids.ecuSerialHex + offset,
        sizeof(livePids.ecuSerialHex) - offset,
        i == 0 ? "%02X" : "-%02X",
        value[i]
      );
      if (written <= 0)
      {
        break;
      }
      offset += static_cast<size_t>(written);
    }
    livePids.ecuSerialValid = offset > 0;
    if (livePids.ecuSerialValid)
    {
      Serial.printf("[PID] ECU SN: %s\n", livePids.ecuSerialHex);
      return true;
    }
  }

  if (did == FORD_DID_ENGINE_SPEED && valueLength >= 2)
  {
    const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
    livePids.didEngineRpm = static_cast<float>(raw) / 4.0f;
    livePids.didEngineRpmValid = true;
    Serial.printf("[PID] Engine RPM DID: %.0f RPM\n", livePids.didEngineRpm);
    return true;
  }

  return false;
}

void parseDidResponsePayload(const uint32_t canId, const uint8_t* payload, const size_t payloadLength)
{
  if (!payload || payloadLength < 3)
  {
    return;
  }

  const uint16_t did = static_cast<uint16_t>(payload[0] << 8) | payload[1];
  const uint8_t* value = &payload[2];
  const size_t valueLength = payloadLength - 2;
  updateCustomDidSnapshot(canId, did, value, valueLength);
}

void markFordModuleSeen(const uint32_t canId)
{
  if (!isFordResponse(canId))
  {
    return;
  }

  const int moduleIndex = fordModuleIndexByResponseId(canId);
  if (moduleIndex < 0 || moduleIndex >= 32)
  {
    return;
  }

  const uint32_t previousMask = state.fordModuleMask;
  state.fordModuleMask |= (1UL << static_cast<uint8_t>(moduleIndex));
  if (state.fordModuleMask != previousMask)
  {
    Serial.printf("[FORD] Module online: %s (0x%03lX)\n", fordModuleNameByResponseId(canId), static_cast<unsigned long>(canId));
  }
}

void appendFloatOrNull(String& json, const char* key, const bool valid, const float value, const uint8_t decimals)
{
  json += "\"";
  json += key;
  json += "\":";
  if (valid)
  {
    json += String(value, static_cast<unsigned int>(decimals));
  }
  else
  {
    json += "null";
  }
}

void updateLivePidSnapshot(const uint8_t pid, const uint8_t* value, const size_t valueLength)
{
  if (value == nullptr)
  {
    return;
  }

  if (isSupportBitmapPid(pid))
  {
    updatePidSupportMap(pid, value, valueLength);
    return;
  }

  switch (pid)
  {
    case FordPids::ENGINE_LOAD:
      if (valueLength >= 1)
      {
        livePids.loadPct = static_cast<float>(value[0]) * 100.0f / 255.0f;
        livePids.loadValid = true;
      }
      break;
    case FordPids::SHORT_TERM_FUEL_TRIM_B1:
      if (valueLength >= 1)
      {
        livePids.stftB1Pct = (static_cast<int>(value[0]) - 128) * 100.0f / 128.0f;
        livePids.stftB1Valid = true;
      }
      break;
    case FordPids::LONG_TERM_FUEL_TRIM_B1:
      if (valueLength >= 1)
      {
        livePids.ltftB1Pct = (static_cast<int>(value[0]) - 128) * 100.0f / 128.0f;
        livePids.ltftB1Valid = true;
      }
      break;
    case FordPids::FUEL_PRESSURE:
      if (valueLength >= 1)
      {
        livePids.fuelPressureKpa = static_cast<float>(value[0]) * 3.0f;
        livePids.fuelPressureValid = true;
      }
      break;
    case FordPids::INTAKE_MAP:
      if (valueLength >= 1)
      {
        livePids.intakeMapKpa = static_cast<float>(value[0]);
        livePids.intakeMapValid = true;
        livePids.boostKpa = livePids.intakeMapKpa > 101.3f ? livePids.intakeMapKpa - 101.3f : 0.0f;
        livePids.boostValid = true;
      }
      break;
    case FordPids::ENGINE_SPEED:
      if (valueLength >= 2)
      {
        const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
        livePids.rpm = static_cast<float>(raw) / 4.0f;
        livePids.rpmValid = true;
      }
      break;
    case FordPids::VEHICLE_SPEED:
      if (valueLength >= 1)
      {
        livePids.speedKph = static_cast<float>(value[0]);
        livePids.speedValid = true;
      }
      break;
    case FordPids::TIMING_ADVANCE:
      if (valueLength >= 1)
      {
        livePids.timingAdvanceDeg = static_cast<float>(value[0]) / 2.0f - 64.0f;
        livePids.timingValid = true;
      }
      break;
    case FordPids::INTAKE_AIR_TEMP:
      if (valueLength >= 1)
      {
        livePids.intakeAirC = static_cast<float>(static_cast<int>(value[0]) - 40);
        livePids.intakeAirValid = true;
      }
      break;
    case FordPids::MAF_RATE:
      if (valueLength >= 2)
      {
        const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
        livePids.mafGps = static_cast<float>(raw) / 100.0f;
        livePids.mafValid = true;
      }
      break;
    case FordPids::O2_SENSOR_1:
      if (valueLength >= 1)
      {
        livePids.o2s1V = static_cast<float>(value[0]) / 200.0f;
        livePids.o2s1Valid = true;
      }
      break;
    case FordPids::COOLANT_TEMP:
      if (valueLength >= 1)
      {
        livePids.coolantC = static_cast<float>(static_cast<int>(value[0]) - 40);
        livePids.coolantValid = true;
      }
      break;
    case FordPids::RUN_TIME_SINCE_START:
      if (valueLength >= 2)
      {
        const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
        livePids.runtimeSeconds = static_cast<float>(raw);
        livePids.runtimeValid = true;
      }
      break;
    case FordPids::FUEL_LEVEL:
      if (valueLength >= 1)
      {
        livePids.fuelPct = static_cast<float>(value[0]) * 100.0f / 255.0f;
        livePids.fuelValid = true;
      }
      break;
    case FordPids::DISTANCE_SINCE_DTC_CLEAR:
      if (valueLength >= 2)
      {
        const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
        livePids.distanceKm = static_cast<float>(raw);
        livePids.distanceValid = true;
      }
      break;
    case FordPids::CONTROL_MODULE_VOLTAGE:
      if (valueLength >= 2)
      {
        const uint16_t raw = static_cast<uint16_t>(value[0] << 8) | value[1];
        livePids.voltage = static_cast<float>(raw) / 1000.0f;
        livePids.voltageValid = true;
      }
      break;
    case FordPids::AMBIENT_AIR_TEMP:
      if (valueLength >= 1)
      {
        livePids.ambientC = static_cast<float>(static_cast<int>(value[0]) - 40);
        livePids.ambientValid = true;
      }
      break;
    case FordPids::ENGINE_OIL_TEMP:
      if (valueLength >= 1)
      {
        livePids.oilTempC = static_cast<float>(static_cast<int>(value[0]) - 40);
        livePids.oilTempValid = true;
      }
      break;
    case FordPids::THROTTLE_POSITION:
      if (valueLength >= 1)
      {
        livePids.throttlePct = static_cast<float>(value[0]) * 100.0f / 255.0f;
        livePids.throttleValid = true;
      }
      break;
    default:
      break;
  }
}

void updateEspNowVehicleCache()
{
  if (!ESP_NOW_TELEMETRY_ENABLED)
  {
    return;
  }

  greatscan::VehicleDataCache cache {};

  cache.engineRpm = livePids.rpmValid ? clampToU16(livePids.rpm) : 0;
  cache.vehicleSpeedKphX10 = livePids.speedValid ? clampToI16(livePids.speedKph, 10.0f) : 0;
  cache.coolantTempCX10 = livePids.coolantValid ? clampToI16(livePids.coolantC, 10.0f) : 0;
  cache.transTempCX10 = livePids.atfValid ? clampToI16(livePids.atfC, 10.0f) : 0;
  cache.oilTempCX10 = livePids.oilTempValid ? clampToI16(livePids.oilTempC, 10.0f) : 0;
  cache.boostKpaX10 = livePids.boostValid ? clampToI16(livePids.boostKpa, 10.0f) : 0;
  cache.batteryMv = livePids.voltageValid ? clampToU16(livePids.voltage, 1000.0f) : 0;
  cache.selectedGear = static_cast<uint8_t>(greatscan::GearState::Unknown);
  cache.fuelPctX10 = livePids.fuelValid ? clampToI16(livePids.fuelPct, 10.0f) : greatscan::kGaugeI16Unavailable;
  cache.throttlePctX10 = livePids.throttleValid ? clampToI16(livePids.throttlePct, 10.0f) : greatscan::kGaugeI16Unavailable;
  cache.dpfPctX10 = livePids.dpfValid ? clampToI16(livePids.dpfPct, 10.0f) : greatscan::kGaugeI16Unavailable;
  // Ford Ranger PX2 does not expose a dedicated oil pressure PID; fuel rail
  // pressure is used as the closest available substitute, matching the
  // "fuel_pressure" -> oil_pressure fallback already used by the HTTP API.
  cache.oilPressureKpaX10 = livePids.fuelPressureValid ? clampToI16(livePids.fuelPressureKpa, 10.0f) : greatscan::kGaugeI16Unavailable;
  cache.eg3TempCX10 = livePids.eg3Valid ? clampToI16(livePids.eg3C, 10.0f) : greatscan::kGaugeI16Unavailable;
  cache.o2s1Mv = livePids.o2s1Valid ? clampToI16(livePids.o2s1V, 1000.0f) : greatscan::kGaugeI16Unavailable;
  cache.engineSpeedDidRpm = livePids.didEngineRpmValid ? clampToI16(livePids.didEngineRpm) : greatscan::kGaugeI16Unavailable;

  uint32_t statusFlags = greatscan::WarningNone;
  // A driver "started" only means the local transceiver/SPI init succeeded --
  // that happens even with no vehicle connected. Only treat the vehicle link
  // as online if a real CAN frame has actually been seen recently.
  constexpr unsigned long kCanTrafficStaleMs = 3000;
  const unsigned long nowMs = millis();
  const bool can1Live = state.can1.status.detectedTraffic && (nowMs - state.can1.status.lastFrameMs < kCanTrafficStaleMs);
  const bool can2Live = state.can2.status.detectedTraffic && (nowMs - state.can2.status.lastFrameMs < kCanTrafficStaleMs);
  if (!can1Live && !can2Live)
  {
    statusFlags |= greatscan::WarningCanOffline;
  }
  if (livePids.coolantValid && livePids.coolantC >= 110.0f)
  {
    statusFlags |= greatscan::WarningEngineHot;
  }
  if (livePids.atfValid && livePids.atfC >= 120.0f)
  {
    statusFlags |= greatscan::WarningAtfHot;
  }
  if (livePids.oilTempValid && livePids.oilTempC >= 130.0f)
  {
    statusFlags |= greatscan::WarningOilHot;
  }
  if (livePids.voltageValid && livePids.voltage <= 11.8f)
  {
    statusFlags |= greatscan::WarningLowVoltage;
  }
  cache.statusFlags = statusFlags;

  espNowManager.updateVehicleCache(cache);
}

String buildStatusJson()
{
  String json = "{";
  json += "\"diag\":\"" + String(diagnosticBusName(resolveBus())) + "\",";
  json += "\"can1\":\"" + String(state.can1.activeLabel) + "\",";
  json += "\"can2\":\"" + String(state.can2.activeLabel) + "\",";
  json += "\"vin\":\"" + String(uiState.vin) + "\",";
  json += "\"dtc\":" + String(uiState.dtcCount) + ",";
  appendFloatOrNull(json, "rpm", livePids.rpmValid, livePids.rpm, 0);
  json += ",";
  appendFloatOrNull(json, "speed", livePids.speedValid, livePids.speedKph, 0);
  json += ",";
  appendFloatOrNull(json, "boost", livePids.boostValid, livePids.boostKpa, 0);
  json += ",";
  appendFloatOrNull(json, "coolant", livePids.coolantValid, livePids.coolantC, 1);
  json += ",";
  appendFloatOrNull(json, "load", livePids.loadValid, livePids.loadPct, 1);
  json += ",";
  appendFloatOrNull(json, "stft_b1", livePids.stftB1Valid, livePids.stftB1Pct, 1);
  json += ",";
  appendFloatOrNull(json, "ltft_b1", livePids.ltftB1Valid, livePids.ltftB1Pct, 1);
  json += ",";
  appendFloatOrNull(json, "fuel_pressure", livePids.fuelPressureValid, livePids.fuelPressureKpa, 0);
  json += ",";
  appendFloatOrNull(json, "map", livePids.intakeMapValid, livePids.intakeMapKpa, 0);
  json += ",";
  appendFloatOrNull(json, "timing", livePids.timingValid, livePids.timingAdvanceDeg, 1);
  json += ",";
  appendFloatOrNull(json, "iat", livePids.intakeAirValid, livePids.intakeAirC, 1);
  json += ",";
  appendFloatOrNull(json, "maf", livePids.mafValid, livePids.mafGps, 2);
  json += ",";
  appendFloatOrNull(json, "o2s1", livePids.o2s1Valid, livePids.o2s1V, 3);
  json += ",";
  appendFloatOrNull(json, "runtime", livePids.runtimeValid, livePids.runtimeSeconds, 0);
  json += ",";
  appendFloatOrNull(json, "fuel", livePids.fuelValid, livePids.fuelPct, 1);
  json += ",";
  appendFloatOrNull(json, "distance", livePids.distanceValid, livePids.distanceKm, 0);
  json += ",";
  appendFloatOrNull(json, "voltage", livePids.voltageValid, livePids.voltage, 3);
  json += ",";
  appendFloatOrNull(json, "ambient", livePids.ambientValid, livePids.ambientC, 1);
  json += ",";
  appendFloatOrNull(json, "oil_temp", livePids.oilTempValid, livePids.oilTempC, 1);
  json += ",";
  appendFloatOrNull(json, "throttle", livePids.throttleValid, livePids.throttlePct, 1);
  json += ",";
  appendFloatOrNull(json, "atf", livePids.atfValid, livePids.atfC, 1);
  json += ",";
  appendFloatOrNull(json, "eg3", livePids.eg3Valid, livePids.eg3C, 1);
  json += ",";
  appendFloatOrNull(json, "engine_speed_did", livePids.didEngineRpmValid, livePids.didEngineRpm, 0);
  json += ",\"ecu_serial\":";
  if (livePids.ecuSerialValid)
  {
    json += "\"" + String(livePids.ecuSerialHex) + "\"";
  }
  else
  {
    json += "null";
  }
  json += ",\"pid_support_discovered\":";
  json += pidSupport.discovered ? "true" : "false";
  json += ",";
  appendFloatOrNull(json, "dpf", livePids.dpfValid, livePids.dpfPct, 1);
  json += ",\"dtc_stored\":" + String(diagnostics.storedCount);
  json += ",\"dtc_pending\":" + String(diagnostics.pendingCount);
  json += ",\"dtc_permanent\":" + String(diagnostics.permanentCount);
  json += ",\"last_stored\":\"" + String(diagnostics.lastStored) + "\"";
  json += ",\"last_pending\":\"" + String(diagnostics.lastPending) + "\"";
  json += ",\"last_permanent\":\"" + String(diagnostics.lastPermanent) + "\"";
  json += ",\"ford_module_mask\":" + String(state.fordModuleMask);
  json += "}";
  return json;
}

void publishGscanBridgeStatus()
{
  if (!GSERIAL_BRIDGE_ENABLED)
  {
    return;
  }

  const unsigned long now = millis();
  if (now - lastBridgePublishMs < GSERIAL_BRIDGE_INTERVAL_MS)
  {
    return;
  }
  lastBridgePublishMs = now;

  const String json = buildStatusJson();
  gscanBridgeSerial.print("@GS");
  gscanBridgeSerial.println(json);
  ++bridgeStatusPublishCount;
  if ((bridgeStatusPublishCount % 20UL) == 0UL)
  {
    Serial.printf("[GS-BRIDGE] tx frames=%lu\n", bridgeStatusPublishCount);
  }
}

void pollGscanBridgeCommands()
{
  if (!GSERIAL_BRIDGE_ENABLED)
  {
    return;
  }

  while (gscanBridgeSerial.available() > 0)
  {
    const char c = static_cast<char>(gscanBridgeSerial.read());
    if (c == '\r')
    {
      continue;
    }

    if (c != '\n')
    {
      if (bridgeCommandBuffer.length() < 32)
      {
        bridgeCommandBuffer += c;
      }
      continue;
    }

    if (!bridgeCommandBuffer.startsWith("@GC") || bridgeCommandBuffer.length() < 4)
    {
      bridgeCommandBuffer = "";
      continue;
    }

    const char command = bridgeCommandBuffer[3];
    bridgeCommandBuffer = "";
    Serial.printf("[GS-BRIDGE] RX cmd=%c\n", command);
    handleSerialCommand(command);
    gscanBridgeSerial.print("@GA");
    gscanBridgeSerial.print(command);
    gscanBridgeSerial.println(" ok");
  }
}

void syncUiBusStatus()
{
  uiStateSetBusStatus(
    uiState,
    state.can1.status,
    state.can1.activeLabel,
    state.can2.status,
    state.can2.activeLabel
  );
}

DisplayPage toDisplayPage(const UiPage page)
{
  switch (page)
  {
    case UiPage::Overview:
      return DisplayPage::Overview;
    case UiPage::LivePids:
      return DisplayPage::LivePids;
    case UiPage::VehicleInfo:
      return DisplayPage::VehicleInfo;
    case UiPage::Faults:
      return DisplayPage::Faults;
    case UiPage::Help:
    default:
      return DisplayPage::Help;
  }
}

void buildDisplayFrame(DisplayFrame& frame)
{
  frame.currentPage = toDisplayPage(uiState.currentPage);
  snprintf(frame.pageName, sizeof(frame.pageName), "%s", uiPageName(uiState.currentPage));
  snprintf(frame.diagnosticBus, sizeof(frame.diagnosticBus), "%s", diagnosticBusName(uiState.diagnosticBus));
  frame.can1Started = uiState.can1Started;
  frame.can2Started = uiState.can2Started;
  snprintf(frame.can1Bitrate, sizeof(frame.can1Bitrate), "%s", uiState.can1Bitrate);
  snprintf(frame.can2Bitrate, sizeof(frame.can2Bitrate), "%s", uiState.can2Bitrate);
  snprintf(frame.vin, sizeof(frame.vin), "%s", uiState.vin);
  frame.dtcCount = uiState.dtcCount;
  snprintf(frame.lastDtc, sizeof(frame.lastDtc), "%s", uiState.lastDtc);
  snprintf(frame.status, sizeof(frame.status), "%s", uiState.status);

  for (size_t index = 0; index < DISPLAY_PID_SLOTS; ++index)
  {
    frame.pids[index].valid = uiState.pids[index].valid;
    frame.pids[index].label[0] = '\0';
    frame.pids[index].value[0] = '\0';
    if (uiState.pids[index].valid)
    {
      snprintf(frame.pids[index].label, sizeof(frame.pids[index].label), "%s", uiState.pids[index].label);
      snprintf(frame.pids[index].value, sizeof(frame.pids[index].value), "%s", uiState.pids[index].value);
    }
  }
}

void renderUiIfDirty()
{
  if (waveshareUiEnabled && uiState.dirty)
  {
    buildDisplayFrame(displayFrame);
    displayManager.render(displayFrame, Serial);
    uiStateMarkClean(uiState);
  }
}

void handleUiAction(const WaveshareUiAction action)
{
  switch (action)
  {
    case WaveshareUiAction::NextPage:
      uiStateNextPage(uiState);
      uiStateSetStatus(uiState, uiPageName(uiState.currentPage));
      break;
    case WaveshareUiAction::PreviousPage:
      uiStatePreviousPage(uiState);
      uiStateSetStatus(uiState, uiPageName(uiState.currentPage));
      break;
    case WaveshareUiAction::Refresh:
      requestFullDiagnosticSweep();
      uiStateSetStatus(uiState, "refresh requested");
      break;
    case WaveshareUiAction::None:
    default:
      break;
  }
}

void printCommandMenu()
{
  Serial.println("[CMD] Commands:");
  Serial.println("[CMD]   m = show this menu");
  Serial.println("[CMD]   h = send diagnostic heartbeat");
  Serial.println("[CMD]   p = request next live PID");
  Serial.println("[CMD]   v = request VIN");
  Serial.println("[CMD]   c = request calibration ID");
  Serial.println("[CMD]   u = request CVN");
  Serial.println("[CMD]   e = request ECU name");
  Serial.println("[CMD]   d = request DTCs");
  Serial.println("[CMD]   o = scan Ford module availability");
  Serial.println("[CMD]   r = scan Ford module DTCs (stored/pending/permanent)");
  Serial.println("[CMD]   g = print Ford module DTC summary");
  Serial.println("[CMD]   k = guarded clear DTCs (pre-check + online modules + verify)");
  Serial.println("[CMD]   x = run diagnostics function (stored/pending/permanent DTC)");
  Serial.println("[CMD]   t = full diagnostic sweep");
  Serial.println("[CMD]   1 = force CAN1 diagnostics");
  Serial.println("[CMD]   2 = force CAN2 diagnostics");
  Serial.println("[CMD]   a = auto-select diagnostic bus");
  Serial.println("[CMD]   s = print status");
}

void printRuntimeStatus()
{
  if (state.can1.status.started)
  {
    twai_status_info_t can1Status {};
    if (twai_get_status_info(&can1Status) == ESP_OK)
    {
      Serial.printf(
        "[STATUS] CAN1 bitrate=%s tx=%lu rx=%lu err=%lu state=%d\n",
        state.can1.activeLabel,
        static_cast<unsigned long>(can1Status.msgs_to_tx),
        static_cast<unsigned long>(can1Status.msgs_to_rx),
        static_cast<unsigned long>(can1Status.tx_error_counter + can1Status.rx_error_counter),
        static_cast<int>(can1Status.state)
      );
    }
  }

  Serial.printf(
    "[STATUS] diag=%s CAN2 bitrate=%s traffic=%s ford_mask=0x%08lX\n",
    diagnosticBusName(resolveBus()),
    state.can2.activeLabel,
    state.can2.status.detectedTraffic ? "seen" : "idle",
    static_cast<unsigned long>(state.fordModuleMask)
  );

  char statusLine[UI_TEXT_SIZE] = {};
  snprintf(
    statusLine,
    sizeof(statusLine),
    "diag=%s can2=%s/%s",
    diagnosticBusName(state.diagnosticBus),
    state.can2.activeLabel,
    state.can2.status.detectedTraffic ? "seen" : "idle"
  );
  uiStateSetStatus(uiState, statusLine);
  syncUiBusStatus();
  printFordModuleAvailability();

}

void requestFullDiagnosticSweep()
{
  Serial.println("[SCAN] Starting full diagnostic sweep");
  requestFordModuleAvailability();
  requestFordModuleDiagnostics();
  requestSupportedPidMaps();
  sendHeartbeat();
  sendVehicleInfoRequest(0x01);
  sendVehicleInfoRequest(0x02);
  sendVehicleInfoRequest(0x03);
  sendVehicleInfoRequest(0x0A);
  sendReadDtcRequest();
  runDiagnosticsFunction();
  for (const auto pid : PID_SEQUENCE)
  {
    if (!shouldPollPid(pid))
    {
      continue;
    }
    Serial.printf("[SCAN] Sweep PID %s (0x%02X)\n", pidName(pid), pid);
    sendPidRequest(pid);
  }

  for (const auto& did : CUSTOM_DIDS)
  {
    Serial.printf("[SCAN] Sweep DID %s (0x%04X)\n", did.name, did.did);
    sendCustomDidRequest(did.requestId, did.did);
  }
}

void handleSerialCommand(const char command)
{
  switch (command)
  {
    case 'm':
    case '?':
      printCommandMenu();
      break;
    case 'h':
      sendHeartbeat();
      break;
    case 'p':
      sendNextPid();
      state.lastPidMillis = millis();
      break;
    case 'v':
      sendVehicleInfoRequest(0x01);
      state.lastVehicleInfoMillis = millis();
      break;
    case 'c':
      sendVehicleInfoRequest(0x02);
      break;
    case 'u':
      sendVehicleInfoRequest(0x03);
      break;
    case 'e':
      sendVehicleInfoRequest(0x0A);
      break;
    case 'd':
      sendReadDtcRequest();
      state.lastDtcMillis = millis();
      break;
    case 'o':
      requestFordModuleAvailability();
      break;
    case 'r':
      requestFordModuleDiagnostics();
      state.lastDtcMillis = millis();
      break;
    case 'g':
      printFordModuleDiagnosticSummary();
      break;
    case 'k':
      runSafeClearDtcWorkflow();
      break;
    case 'x':
      runDiagnosticsFunction();
      state.lastDtcMillis = millis();
      break;
    case 't':
      requestFullDiagnosticSweep();
      break;
    case '[':
      uiStatePreviousPage(uiState);
      uiStateSetStatus(uiState, uiPageName(uiState.currentPage));
      break;
    case ']':
      uiStateNextPage(uiState);
      uiStateSetStatus(uiState, uiPageName(uiState.currentPage));
      break;
    case '1':
      selectDiagnosticBus(DiagnosticBus::Can1);
      break;
    case '2':
      selectDiagnosticBus(DiagnosticBus::Can2);
      break;
    case 'a':
      state.diagnosticBus = DiagnosticBus::None;
      Serial.println("[SCAN] Diagnostic bus returned to auto-select");
      break;
    case 's':
      printRuntimeStatus();
      break;
    default:
      if (command != '\r' && command != '\n' && command != ' ')
      {
        Serial.printf("[CMD] Unknown command: %c\n", command);
      }
      break;
  }
}

void pollSerialCommands()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    handleSerialCommand(command);
  }
}

void stopCan1()
{
  if (!state.can1.status.started)
  {
    return;
  }

  twai_stop();
  twai_driver_uninstall();
  state.can1.status.started = false;
}

void stopCan2()
{
  if (!state.can2.status.started)
  {
    return;
  }

  can2Controller.reset();
  state.can2.status.started = false;
}

void printBoardProfile()
{
  Serial.println("[BOARD] Autosport Labs ESP32-CAN-X2");
  if (hasStatusLed())
  {
    Serial.printf("[BOARD] STATUS LED=GPIO%u\n", Pins::STATUS_LED);
  }
  else
  {
    Serial.println("[BOARD] STATUS LED=disabled (no configured GPIO)");
  }
  Serial.printf("[BOARD] CAN1 TWAI RX=GPIO%u TX=GPIO%u\n", Pins::CAN1_RX, Pins::CAN1_TX);
  Serial.printf(
    "[BOARD] CAN2 MCP2515 CS=%d CLK=%d MISO=%d MOSI=%d IRQ=GPIO%u\n",
    Pins::CAN2_CS,
    Pins::CAN2_CLK,
    Pins::CAN2_MISO,
    Pins::CAN2_MOSI,
    Pins::CAN2_IRQ
  );
}

void selectDiagnosticBus(const DiagnosticBus bus)
{
  if (bus == DiagnosticBus::None || state.diagnosticBus == bus)
  {
    return;
  }

  state.diagnosticBus = bus;
  Serial.printf("[SCAN] Diagnostic bus selected: %s\n", diagnosticBusName(bus));
  uiStateSetBus(uiState, bus);
}

DiagnosticBus resolveBus()
{
  state.diagnosticBus = resolveDiagnosticBus(
    state.diagnosticBus,
    state.can1.status,
    state.can2.status
  );
  return state.diagnosticBus;
}

void logFrame(const char* tag, const twai_message_t& frame)
{
  char dataPart[80] = {};
  size_t offset = 0;
  for (int index = 0; index < frame.data_length_code && offset + 4 < sizeof(dataPart); ++index)
  {
    const int written = snprintf(dataPart + offset, sizeof(dataPart) - offset, "%02X", frame.data[index]);
    if (written <= 0)
    {
      break;
    }
    offset += static_cast<size_t>(written);
    if (index + 1 < frame.data_length_code && offset + 2 < sizeof(dataPart))
    {
      dataPart[offset++] = ' ';
      dataPart[offset] = '\0';
    }
  }

  char line[192] = {};
  snprintf(line, sizeof(line), "[%s] ID:%03lX DLC:%u DATA:%s", tag, frame.identifier, frame.data_length_code, dataPart);
  markRawLine(line);

  Serial.printf("[%s] ID: 0x%03lX DLC: %u Data:", tag, frame.identifier, frame.data_length_code);
  for (int index = 0; index < frame.data_length_code; ++index)
  {
    Serial.printf(" %02X", frame.data[index]);
  }
  Serial.println();
}

void logFrame(const char* tag, const can_frame& frame)
{
  char dataPart[80] = {};
  size_t offset = 0;
  for (int index = 0; index < frame.can_dlc && offset + 4 < sizeof(dataPart); ++index)
  {
    const int written = snprintf(dataPart + offset, sizeof(dataPart) - offset, "%02X", frame.data[index]);
    if (written <= 0)
    {
      break;
    }
    offset += static_cast<size_t>(written);
    if (index + 1 < frame.can_dlc && offset + 2 < sizeof(dataPart))
    {
      dataPart[offset++] = ' ';
      dataPart[offset] = '\0';
    }
  }

  char line[192] = {};
  snprintf(line, sizeof(line), "[%s] ID:%03lX DLC:%u DATA:%s", tag, static_cast<unsigned long>(frame.can_id & CAN_SFF_MASK), frame.can_dlc, dataPart);
  markRawLine(line);

  Serial.printf("[%s] ID: 0x%03lX DLC: %u Data:", tag, static_cast<unsigned long>(frame.can_id & CAN_SFF_MASK), frame.can_dlc);
  for (int index = 0; index < frame.can_dlc; ++index)
  {
    Serial.printf(" %02X", frame.data[index]);
  }
  Serial.println();
}

bool installCan1Driver(const CanBitrateOption& option)
{
  stopCan1();

  const twai_general_config_t generalConfig = TWAI_GENERAL_CONFIG_DEFAULT(
    Pins::CAN1_TX,
    Pins::CAN1_RX,
    TWAI_MODE_NORMAL
  );
  const twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t result = twai_driver_install(&generalConfig, &option.timing, &filterConfig);
  if (result != ESP_OK)
  {
    Serial.printf("[CANBUS] Install failed at %s (err=%d)\n", option.label, result);
    return false;
  }

  result = twai_start();
  if (result != ESP_OK)
  {
    Serial.printf("[CANBUS] Start failed at %s (err=%d)\n", option.label, result);
    twai_driver_uninstall();
    return false;
  }

  state.can1.status.started = true;
  state.can1.activeBitrate = option.bitrate;
  state.can1.activeLabel = option.label;
  Serial.printf("[CANBUS] CAN1 active at %s\n", option.label);
  syncUiBusStatus();
  return true;
}

bool installCan2Driver(const CanBitrateOption& option)
{
  stopCan2();

  pinMode(Pins::CAN2_CS, OUTPUT);
  digitalWrite(Pins::CAN2_CS, HIGH);
  pinMode(Pins::CAN2_IRQ, INPUT_PULLUP);
  SPI.begin(Pins::CAN2_CLK, Pins::CAN2_MISO, Pins::CAN2_MOSI, Pins::CAN2_CS);

  if (can2Controller.reset() != MCP2515::ERROR_OK)
  {
    Serial.printf("[CANBUS] CAN2 reset failed at %s\n", option.label);
    return false;
  }

  if (can2Controller.setBitrate(option.mcpSpeed, MCP_16MHZ) != MCP2515::ERROR_OK)
  {
    Serial.printf("[CANBUS] CAN2 bitrate set failed at %s\n", option.label);
    return false;
  }

  if (can2Controller.setNormalMode() != MCP2515::ERROR_OK)
  {
    Serial.printf("[CANBUS] CAN2 normal-mode failed at %s\n", option.label);
    return false;
  }

  state.can2.status.started = true;
  state.can2.activeBitrate = option.bitrate;
  state.can2.activeLabel = option.label;
  Serial.printf("[CANBUS] CAN2 active at %s\n", option.label);
  syncUiBusStatus();
  return true;
}

bool sendCan1RequestToId(const uint32_t requestId, const uint8_t* payload, const size_t payloadLength)
{
  if (!state.can1.status.started || payloadLength > 8)
  {
    return false;
  }

  twai_message_t frame {};
  frame.identifier = requestId;
  frame.extd = 0;
  frame.rtr = 0;
  frame.self = 0;
  frame.ss = 0;
  frame.data_length_code = static_cast<uint8_t>(payloadLength);
  memcpy(frame.data, payload, payloadLength);

  const esp_err_t result = twai_transmit(&frame, pdMS_TO_TICKS(100));
  if (result != ESP_OK)
  {
    Serial.printf("[TX] Request failed (err=%d)\n", result);
    return false;
  }

  logFrame("TX", frame);
  return true;
}

bool sendCan2RequestToId(const uint32_t requestId, const uint8_t* payload, const size_t payloadLength)
{
  if (!state.can2.status.started || payloadLength > 8)
  {
    return false;
  }

  can_frame frame {};
  frame.can_id = requestId;
  frame.can_dlc = static_cast<uint8_t>(payloadLength);
  memcpy(frame.data, payload, payloadLength);

  const MCP2515::ERROR result = can2Controller.sendMessage(&frame);
  if (result != MCP2515::ERROR_OK)
  {
    Serial.printf("[TX2] Request failed (err=%d)\n", static_cast<int>(result));
    return false;
  }

  logFrame("TX2", frame);
  return true;
}

bool sendCan1Request(const uint8_t* payload, const size_t payloadLength)
{
  return sendCan1RequestToId(FordIds::BROADCAST_REQUEST, payload, payloadLength);
}

bool sendCan2Request(const uint8_t* payload, const size_t payloadLength)
{
  return sendCan2RequestToId(FordIds::BROADCAST_REQUEST, payload, payloadLength);
}

bool sendRequestToId(const uint32_t requestId, const uint8_t* payload, const size_t payloadLength)
{
  const DiagnosticBus bus = resolveBus();
  if (bus == DiagnosticBus::Can2)
  {
    return sendCan2RequestToId(requestId, payload, payloadLength);
  }
  return sendCan1RequestToId(requestId, payload, payloadLength);
}

bool sendRequestToSelectedBus(const uint8_t* payload, const size_t payloadLength)
{
  const DiagnosticBus bus = resolveBus();
  if (bus == DiagnosticBus::Can2)
  {
    return sendCan2Request(payload, payloadLength);
  }
  return sendCan1Request(payload, payloadLength);
}

bool sendHeartbeat()
{
  const uint8_t payload[8] = {0x02, FordServices::DIAGNOSTIC_SESSION_CONTROL, 0x0C, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendPidRequest(const uint8_t pid)
{
  const uint8_t payload[8] = {0x02, FordServices::SHOW_CURRENT_DATA, pid, 0x00, 0x00, 0x00, 0x00, 0x00};
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendCustomDidRequest(const uint32_t requestId, const uint16_t did)
{
  const uint8_t payload[8] = {
    0x03,
    FORD_SVC_READ_DID,
    static_cast<uint8_t>((did >> 8) & 0xFF),
    static_cast<uint8_t>(did & 0xFF),
    0x00,
    0x00,
    0x00,
    0x00
  };
  return sendRequestToId(requestId, payload, sizeof(payload));
}

bool sendVehicleInfoRequest(const uint8_t infoType)
{
  const uint8_t payload[8] = {0x02, FordServices::REQUEST_VEHICLE_INFO, infoType, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial.printf("[SCAN] Requesting %s (0x%02X)\n", infoTypeName(infoType), infoType);
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendReadDtcRequest()
{
  const uint8_t payload[8] = {0x02, FordServices::READ_DTC_BY_STATUS, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial.println("[SCAN] Requesting DTCs");
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendReadStoredDtcRequest()
{
  const uint8_t payload[8] = {0x01, FordServices::READ_STORED_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial.println("[SCAN] Requesting stored DTCs (Mode 03)");
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendReadPendingDtcRequest()
{
  const uint8_t payload[8] = {0x01, FordServices::READ_PENDING_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial.println("[SCAN] Requesting pending DTCs (Mode 07)");
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendReadPermanentDtcRequest()
{
  const uint8_t payload[8] = {0x01, FordServices::READ_PERMANENT_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial.println("[SCAN] Requesting permanent DTCs (Mode 0A)");
  return sendRequestToSelectedBus(payload, sizeof(payload));
}

bool sendClearDtcRequest()
{
  const uint8_t payload[8] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  bool sent = sendRequestToSelectedBus(payload, sizeof(payload));
  Serial.println("[SCAN] Clearing DTCs on selected bus");

  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }
    sent = sendRequestToId(module->requestId, payload, sizeof(payload)) || sent;
  }

  return sent;
}

bool runSafeClearDtcWorkflow()
{
  if (state.clearWorkflowActive)
  {
    Serial.println("[SCAN] Clear workflow already running");
    return false;
  }

  const bool ignOnEstimate =
    (livePids.rpmValid && livePids.rpm > 0.0f) ||
    (livePids.voltageValid && livePids.voltage >= 12.2f);
  if (!ignOnEstimate)
  {
    Serial.println("[SCAN] Clear blocked: IGN appears OFF (need IGN ON)");
    uiStateSetStatus(uiState, "Clear blocked: IGN OFF");
    return false;
  }

  if (livePids.rpmValid && livePids.rpm > 50.0f)
  {
    Serial.printf("[SCAN] Clear blocked: engine running (rpm=%.0f). Stop engine first.\n", livePids.rpm);
    uiStateSetStatus(uiState, "Clear blocked: stop engine");
    return false;
  }

  if (livePids.speedValid && livePids.speedKph > 0.5f)
  {
    Serial.printf("[SCAN] Clear blocked: vehicle moving (speed=%.1f km/h).\n", livePids.speedKph);
    uiStateSetStatus(uiState, "Clear blocked: vehicle moving");
    return false;
  }

  if (livePids.voltageValid && livePids.voltage < 11.8f)
  {
    Serial.printf("[SCAN] Clear blocked: low battery voltage (%.2fV).\n", livePids.voltage);
    uiStateSetStatus(uiState, "Clear blocked: low battery");
    return false;
  }

  const uint8_t payload[8] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  state.clearAttemptMask = 0;
  state.clearAckMask = 0;
  state.clearAttemptCount = 0;
  state.clearAckCount = 0;

  bool sentAny = false;
  bool sentToOnlineModules = false;
  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    const bool online = (state.fordModuleMask & (1UL << static_cast<uint8_t>(index))) != 0;
    if (!online)
    {
      continue;
    }

    if (sendRequestToId(module->requestId, payload, sizeof(payload)))
    {
      sentAny = true;
      sentToOnlineModules = true;
      state.clearAttemptMask |= (1UL << static_cast<uint8_t>(index));
      if (state.clearAttemptCount < 255)
      {
        ++state.clearAttemptCount;
      }
    }
  }

  if (!sentAny)
  {
    // Fallback when online module map is not yet populated.
    sentAny = sendRequestToSelectedBus(payload, sizeof(payload));
    if (sentAny && state.clearAttemptCount < 255)
    {
      ++state.clearAttemptCount;
    }
    Serial.println("[SCAN] Clear fallback: broadcast on selected bus");
  }
  else
  {
    Serial.println(sentToOnlineModules ? "[SCAN] Clearing DTCs on known-online modules" : "[SCAN] Clear request sent");
  }

  if (!sentAny)
  {
    Serial.println("[SCAN] Clear request not sent");
    uiStateSetStatus(uiState, "Clear request failed");
    return false;
  }

  state.clearWorkflowActive = true;
  state.clearWorkflowStage = 1;
  state.clearWorkflowStartedMillis = millis();
  uiStateSetStatus(uiState, "Clear sent; verifying...");
  return true;
}

void requestFordModuleAvailability()
{
  const uint8_t payload[8] = {0x02, FordServices::SHOW_CURRENT_DATA, FordPids::SUPPORTED_01_20, 0x00, 0x00, 0x00, 0x00, 0x00};
  state.fordModuleMask = 0;
  state.fordSweepStartedMillis = millis();
  Serial.println("[FORD] Requesting module availability (Mode 01 PID 00)");

  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    Serial.printf("[FORD] Probe %s via 0x%03lX\n", module->name, static_cast<unsigned long>(module->requestId));
    sendRequestToId(module->requestId, payload, sizeof(payload));
  }
}

void requestFordModuleDiagnostics()
{
  Serial.println("[FORD] Requesting per-module diagnostics");
  resetFordModuleDiagnosticSummary();
  state.fordDiagSweepStartedMillis = millis();
  const uint8_t stored[8] = {0x01, FordServices::READ_STORED_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t pending[8] = {0x01, FordServices::READ_PENDING_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t permanent[8] = {0x01, FordServices::READ_PERMANENT_DTC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    Serial.printf("[FORD] DTC sweep %s\n", module->name);
    sendRequestToId(module->requestId, stored, sizeof(stored));
    sendRequestToId(module->requestId, pending, sizeof(pending));
    sendRequestToId(module->requestId, permanent, sizeof(permanent));
  }
}

void printFordModuleAvailability()
{
  Serial.println("[FORD] Module availability summary:");
  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    const bool online = (state.fordModuleMask & (1UL << static_cast<uint8_t>(index))) != 0;
    Serial.printf(
      "[FORD]   %-18s req=0x%03lX resp=0x%03lX %s\n",
      module->name,
      static_cast<unsigned long>(module->requestId),
      static_cast<unsigned long>(module->responseId),
      online ? "online" : "no response"
    );
  }
}

void printFordModuleDiagnosticSummary()
{
  Serial.println("[FORD] Module DTC summary:");
  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    const bool online = (state.fordModuleMask & (1UL << static_cast<uint8_t>(index))) != 0;
    const ModuleDtcSnapshot& snapshot = moduleDiagnostics[index];
    Serial.printf(
      "[FORD]   %-10s online=%s seen=%s stored=%u(%s) pending=%u(%s) permanent=%u(%s)\n",
      module->name,
      online ? "yes" : "no",
      snapshot.seen ? "yes" : "no",
      snapshot.storedCount,
      snapshot.lastStored,
      snapshot.pendingCount,
      snapshot.lastPending,
      snapshot.permanentCount,
      snapshot.lastPermanent
    );
  }
}

void printClearWorkflowSummary()
{
  Serial.printf(
    "[SCAN] Clear summary: attempted=%u ack=%u\n",
    state.clearAttemptCount,
    state.clearAckCount
  );

  for (size_t index = 0; index < fordModuleCount(); ++index)
  {
    const bool attempted = (state.clearAttemptMask & (1UL << static_cast<uint8_t>(index))) != 0;
    if (!attempted)
    {
      continue;
    }

    const FordModule* module = fordModuleByIndex(index);
    if (!module)
    {
      continue;
    }

    const bool acked = (state.clearAckMask & (1UL << static_cast<uint8_t>(index))) != 0;
    Serial.printf("[SCAN]   %-10s clear=%s\n", module->name, acked ? "ACK" : "NO_ACK");
  }
}

void runDiagnosticsFunction()
{
  Serial.println("[SCAN] Running diagnostics function");
  sendReadStoredDtcRequest();
  sendReadPendingDtcRequest();
  sendReadPermanentDtcRequest();
  sendReadDtcRequest();
}

bool sendCan1FlowControlFrame(const uint32_t responseId)
{
  if (!state.can1.status.started)
  {
    return false;
  }

  twai_message_t frame {};
  frame.identifier = responseId - 8;
  frame.extd = 0;
  frame.rtr = 0;
  frame.self = 0;
  frame.ss = 0;
  frame.data_length_code = 8;
  frame.data[0] = FordServices::FLOW_CONTROL;
  frame.data[1] = 0x00;
  frame.data[2] = 0x00;

  if (twai_transmit(&frame, pdMS_TO_TICKS(100)) != ESP_OK)
  {
    return false;
  }

  logFrame("TX", frame);
  return true;
}

bool sendCan2FlowControlFrame(const uint32_t responseId)
{
  if (!state.can2.status.started)
  {
    return false;
  }

  can_frame frame {};
  frame.can_id = responseId - 8;
  frame.can_dlc = 8;
  frame.data[0] = FordServices::FLOW_CONTROL;
  frame.data[1] = 0x00;
  frame.data[2] = 0x00;

  if (can2Controller.sendMessage(&frame) != MCP2515::ERROR_OK)
  {
    return false;
  }

  logFrame("TX2", frame);
  return true;
}

void printPidValue(const uint8_t pid, const uint8_t* value, const size_t valueLength)
{
  updateLivePidSnapshot(pid, value, valueLength);

  char output[64] = {};
  if (formatPidValue(pid, value, valueLength, output, sizeof(output)))
  {
    Serial.printf("[PID] %s\n", output);
    uiStateSetPidValue(uiState, pid, pidName(pid), output);
    return;
  }

  Serial.printf("[PID] %s raw:", pidName(pid));
  for (size_t index = 0; index < valueLength; ++index)
  {
    Serial.printf(" %02X", value[index]);
  }
  Serial.println();
}

void printVehicleInfoValue(const uint8_t infoType, const uint8_t* value, const size_t valueLength)
{
  char output[96] = {};
  if (formatVehicleInfoValue(infoType, value, valueLength, output, sizeof(output)))
  {
    Serial.printf("[FORD] %s\n", output);
    if (infoType == 0x01 && strncmp(output, "VIN: ", 5) == 0)
    {
      uiStateSetVin(uiState, output + 5);
    }
    else
    {
      uiStateSetStatus(uiState, output);
    }
  }
}

void completeIsoTpSession(IsoTpSession& session, const char* busTag)
{
  if (!session.active)
  {
    return;
  }

  if (session.service == FordServices::VEHICLE_INFO_RESPONSE)
  {
    printVehicleInfoValue(session.infoType, session.buffer, session.bytesCollected);
  }
  else if (session.service == FORD_SVC_READ_DID_RESPONSE)
  {
    uint8_t payload[34] = {};
    payload[0] = session.infoType;
    const size_t copyLength = session.bytesCollected < sizeof(payload) - 1 ? session.bytesCollected : sizeof(payload) - 1;
    memcpy(&payload[1], session.buffer, copyLength);
    parseDidResponsePayload(session.sourceId, payload, copyLength + 1);
  }
  else if (session.service == FordServices::READ_DTC_POSITIVE_RESPONSE)
  {
    const char* moduleName = fordModuleNameByResponseId(session.sourceId);
    const int moduleIndex = fordModuleIndexByResponseId(session.sourceId);
    printUdsDtcValue(moduleName, moduleIndex, session.buffer, session.bytesCollected);
  }

  Serial.printf("[ISOTP] %s reassembly complete\n", busTag);
  resetIsoTpSession(session);
}

bool handleCan1IsoTp(const twai_message_t& frame)
{
  const IsoTpUpdate first = handleIsoTpFirstFrame(can1IsoTp, frame.identifier, frame.data, frame.data_length_code);
  if (first == IsoTpUpdate::Started)
  {
    sendCan1FlowControlFrame(frame.identifier);
    return true;
  }

  if (first == IsoTpUpdate::Complete)
  {
    completeIsoTpSession(can1IsoTp, "CAN1");
    return true;
  }

  const IsoTpUpdate next = handleIsoTpConsecutiveFrame(can1IsoTp, frame.identifier, frame.data, frame.data_length_code);
  if (next == IsoTpUpdate::Complete)
  {
    completeIsoTpSession(can1IsoTp, "CAN1");
    return true;
  }

  return next == IsoTpUpdate::Continued;
}

bool handleCan2IsoTp(const can_frame& frame, const uint32_t canId)
{
  const IsoTpUpdate first = handleIsoTpFirstFrame(can2IsoTp, canId, frame.data, frame.can_dlc);
  if (first == IsoTpUpdate::Started)
  {
    sendCan2FlowControlFrame(canId);
    return true;
  }

  if (first == IsoTpUpdate::Complete)
  {
    completeIsoTpSession(can2IsoTp, "CAN2");
    return true;
  }

  const IsoTpUpdate next = handleIsoTpConsecutiveFrame(can2IsoTp, canId, frame.data, frame.can_dlc);
  if (next == IsoTpUpdate::Complete)
  {
    completeIsoTpSession(can2IsoTp, "CAN2");
    return true;
  }

  return next == IsoTpUpdate::Continued;
}

void handleCan1Frame(const twai_message_t& frame)
{
  if (!isFordResponse(frame.identifier))
  {
    return;
  }

  markFordModuleSeen(frame.identifier);
  selectDiagnosticBus(DiagnosticBus::Can1);
  logFrame("RX", frame);

  if (handleCan1IsoTp(frame) || frame.data_length_code < 3)
  {
    return;
  }

  const uint8_t protocolLength = frame.data[0];
  if ((protocolLength >> 4) != 0x0)
  {
    return;
  }

  const uint8_t service = frame.data[1];
  const char* moduleName = fordModuleNameByResponseId(frame.identifier);
  const int moduleIndex = fordModuleIndexByResponseId(frame.identifier);
  if (service == FORD_SVC_READ_DID_RESPONSE && frame.data_length_code >= 6)
  {
    const size_t payloadLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.data_length_code - 2));
    parseDidResponsePayload(frame.identifier, &frame.data[2], payloadLength);
    return;
  }

  if (service == FordServices::CURRENT_DATA_RESPONSE && frame.data_length_code >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 2 : 0), static_cast<size_t>(frame.data_length_code - 3));
    printPidValue(frame.data[2], &frame.data[3], valueLength);
    return;
  }

  if (service == FordServices::VEHICLE_INFO_RESPONSE && frame.data_length_code >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 2 : 0), static_cast<size_t>(frame.data_length_code - 3));
    printVehicleInfoValue(frame.data[2], &frame.data[3], valueLength);
    return;
  }

  if (service == FordServices::READ_DTC_POSITIVE_RESPONSE && frame.data_length_code >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.data_length_code - 2));
    printUdsDtcValue(moduleName, moduleIndex, &frame.data[2], valueLength);
    return;
  }

  if (service == FordServices::STORED_DTC_RESPONSE && frame.data_length_code >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Stored %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.data_length_code - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.storedCount, diagnostics.lastStored, sizeof(diagnostics.lastStored));
    return;
  }

  if (service == FordServices::PENDING_DTC_RESPONSE && frame.data_length_code >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Pending %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.data_length_code - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.pendingCount, diagnostics.lastPending, sizeof(diagnostics.lastPending));
    return;
  }

  if (service == FordServices::PERMANENT_DTC_RESPONSE && frame.data_length_code >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Permanent %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.data_length_code - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.permanentCount, diagnostics.lastPermanent, sizeof(diagnostics.lastPermanent));
    return;
  }

  if (service == 0x44)
  {
    Serial.printf("[FORD] Clear DTC acknowledged by %s (0x%03lX)\n", moduleName, static_cast<unsigned long>(frame.identifier));
    if (state.clearWorkflowActive && moduleIndex >= 0 && moduleIndex < 32)
    {
      const uint32_t bit = (1UL << static_cast<uint8_t>(moduleIndex));
      if ((state.clearAckMask & bit) == 0)
      {
        state.clearAckMask |= bit;
        if (state.clearAckCount < 255)
        {
          ++state.clearAckCount;
        }
      }
    }
    uiStateSetStatus(uiState, "DTC clear acknowledged");
    return;
  }

  if (service == FordServices::SESSION_CONTROL_RESPONSE && frame.data_length_code >= 3)
  {
    Serial.printf("[FORD] Diagnostic session acknowledged, subfunction 0x%02X\n", frame.data[2]);
    return;
  }

  if (service == 0x7F && frame.data_length_code >= 4)
  {
    const uint8_t rejectedService = frame.data[2];
    const uint8_t nrc = frame.data[3];
    Serial.printf(
      "[FORD] Negative response from %s service=0x%02X nrc=0x%02X (%s)\n",
      moduleName,
      rejectedService,
      nrc,
      nrcName(nrc)
    );
    if (rejectedService == FordServices::READ_DTC_BY_STATUS)
    {
      uiStateSetStatus(uiState, "UDS DTC request rejected by module");
    }
  }
}

void handleCan2Frame(const can_frame& frame)
{
  const uint32_t canId = frame.can_id & CAN_EFF_FLAG ? (frame.can_id & CAN_EFF_MASK) : (frame.can_id & CAN_SFF_MASK);

  if (isFordResponse(canId))
  {
    markFordModuleSeen(canId);
    selectDiagnosticBus(DiagnosticBus::Can2);
  }

  logFrame("RX2", frame);

  if (handleCan2IsoTp(frame, canId) || !isFordResponse(canId) || frame.can_dlc < 3)
  {
    return;
  }

  const uint8_t protocolLength = frame.data[0];
  if ((protocolLength >> 4) != 0x0)
  {
    return;
  }

  const uint8_t service = frame.data[1];
  const char* moduleName = fordModuleNameByResponseId(canId);
  const int moduleIndex = fordModuleIndexByResponseId(canId);
  if (service == FORD_SVC_READ_DID_RESPONSE && frame.can_dlc >= 6)
  {
    const size_t payloadLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.can_dlc - 2));
    parseDidResponsePayload(canId, &frame.data[2], payloadLength);
    return;
  }

  if (service == FordServices::CURRENT_DATA_RESPONSE && frame.can_dlc >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 2 : 0), static_cast<size_t>(frame.can_dlc - 3));
    printPidValue(frame.data[2], &frame.data[3], valueLength);
    return;
  }

  if (service == FordServices::VEHICLE_INFO_RESPONSE && frame.can_dlc >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 2 : 0), static_cast<size_t>(frame.can_dlc - 3));
    printVehicleInfoValue(frame.data[2], &frame.data[3], valueLength);
    return;
  }

  if (service == FordServices::READ_DTC_POSITIVE_RESPONSE && frame.can_dlc >= 4)
  {
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.can_dlc - 2));
    printUdsDtcValue(moduleName, moduleIndex, &frame.data[2], valueLength);
    return;
  }

  if (service == FordServices::STORED_DTC_RESPONSE && frame.can_dlc >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Stored %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.can_dlc - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.storedCount, diagnostics.lastStored, sizeof(diagnostics.lastStored));
    return;
  }

  if (service == FordServices::PENDING_DTC_RESPONSE && frame.can_dlc >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Pending %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.can_dlc - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.pendingCount, diagnostics.lastPending, sizeof(diagnostics.lastPending));
    return;
  }

  if (service == FordServices::PERMANENT_DTC_RESPONSE && frame.can_dlc >= 4)
  {
    char tag[48] = {};
    snprintf(tag, sizeof(tag), "Permanent %s", moduleName);
    const size_t valueLength = min(static_cast<size_t>(protocolLength > 1 ? protocolLength - 1 : 0), static_cast<size_t>(frame.can_dlc - 2));
    updateFordModuleDtcSnapshot(moduleIndex, service, &frame.data[2], valueLength);
    printObdDtcPayload(tag, &frame.data[2], valueLength, diagnostics.permanentCount, diagnostics.lastPermanent, sizeof(diagnostics.lastPermanent));
    return;
  }

  if (service == 0x44)
  {
    Serial.printf("[FORD] CAN2 clear DTC acknowledged by %s (0x%03lX)\n", moduleName, static_cast<unsigned long>(canId));
    if (state.clearWorkflowActive && moduleIndex >= 0 && moduleIndex < 32)
    {
      const uint32_t bit = (1UL << static_cast<uint8_t>(moduleIndex));
      if ((state.clearAckMask & bit) == 0)
      {
        state.clearAckMask |= bit;
        if (state.clearAckCount < 255)
        {
          ++state.clearAckCount;
        }
      }
    }
    uiStateSetStatus(uiState, "DTC clear acknowledged");
    return;
  }

  if (service == FordServices::SESSION_CONTROL_RESPONSE)
  {
    Serial.printf("[FORD] CAN2 diagnostic session acknowledged, subfunction 0x%02X\n", frame.data[2]);
    return;
  }

  if (service == 0x7F && frame.can_dlc >= 4)
  {
    const uint8_t rejectedService = frame.data[2];
    const uint8_t nrc = frame.data[3];
    Serial.printf(
      "[FORD] CAN2 negative response from %s service=0x%02X nrc=0x%02X (%s)\n",
      moduleName,
      rejectedService,
      nrc,
      nrcName(nrc)
    );
    if (rejectedService == FordServices::READ_DTC_BY_STATUS)
    {
      uiStateSetStatus(uiState, "UDS DTC request rejected by module");
    }
  }
}

void pollIncomingFrames()
{
  if (state.can1.status.started)
  {
    twai_message_t frame {};
    while (twai_receive(&frame, 0) == ESP_OK)
    {
      state.can1.status.detectedTraffic = true;
      state.can1.status.lastFrameMs = millis();
      handleCan1Frame(frame);
    }
  }

  if (state.can2.status.started)
  {
    can_frame frame {};
    while (can2Controller.readMessage(&frame) == MCP2515::ERROR_OK)
    {
      state.can2.status.detectedTraffic = true;
      state.can2.status.lastFrameMs = millis();
      handleCan2Frame(frame);
    }
  }
}

void sendNextPid()
{
  constexpr size_t kCriticalCount = sizeof(CRITICAL_PID_SEQUENCE) / sizeof(CRITICAL_PID_SEQUENCE[0]);
  constexpr size_t kPidCount = sizeof(PID_SEQUENCE) / sizeof(PID_SEQUENCE[0]);
  // One slow tick (full PID_SEQUENCE round-robin) per full pass through the
  // critical list, so every critical PID gets exactly one dedicated slot per
  // kCriticalScheduleTicks-tick period instead of being diluted further.
  constexpr uint8_t kCriticalScheduleTicks = static_cast<uint8_t>(kCriticalCount + 1);

  // kCriticalCount out of every kCriticalScheduleTicks ticks poll the
  // critical gauge PIDs (RPM, speed, boost/MAP); the remaining tick advances
  // through the full PID_SEQUENCE round-robin (covers voltage, coolant,
  // throttle, fuel, oil-temp and the slower diagnostic-only PIDs). At
  // PID_INTERVAL_MS=100 this refreshes RPM/speed/boost roughly every ~400ms.
  state.pidScheduleTick = static_cast<uint8_t>((state.pidScheduleTick + 1) % kCriticalScheduleTicks);
  if (state.pidScheduleTick != 0)
  {
    for (size_t attempt = 0; attempt < kCriticalCount; ++attempt)
    {
      const uint8_t pid = CRITICAL_PID_SEQUENCE[state.nextCriticalPidIndex];
      state.nextCriticalPidIndex = (state.nextCriticalPidIndex + 1) % kCriticalCount;
      if (!shouldPollPid(pid))
      {
        continue;
      }

      Serial.printf("[SCAN] Requesting %s (0x%02X)\n", pidName(pid), pid);
      sendPidRequest(pid);
      sendNextCustomDid();
      return;
    }
  }

  for (size_t attempt = 0; attempt < kPidCount; ++attempt)
  {
    const uint8_t pid = PID_SEQUENCE[state.nextPidIndex];
    state.nextPidIndex = (state.nextPidIndex + 1) % kPidCount;
    if (!shouldPollPid(pid))
    {
      continue;
    }

    Serial.printf("[SCAN] Requesting %s (0x%02X)\n", pidName(pid), pid);
    sendPidRequest(pid);
    sendNextCustomDid();
    return;
  }

  // Support map may not be available yet; re-request feature bitmaps.
  requestSupportedPidMaps();
  sendNextCustomDid();
}

bool initializeCan1()
{
  const DiagnosticBus previousBus = state.diagnosticBus;
  state.diagnosticBus = DiagnosticBus::Can1;

  for (const auto& option : BITRATES)
  {
    if (!installCan1Driver(option))
    {
      continue;
    }

    Serial.printf("[AUTODETECT] Probing Ford traffic at %s\n", option.label);
    sendHeartbeat();
    delay(50);
    sendPidRequest(FordPids::ENGINE_SPEED);

    const unsigned long probeStart = millis();
    twai_message_t frame {};
    while (millis() - probeStart < 600)
    {
      if (twai_receive(&frame, pdMS_TO_TICKS(20)) != ESP_OK)
      {
        continue;
      }

      if (!isFordResponse(frame.identifier))
      {
        continue;
      }

      state.can1.status.detectedTraffic = true;
      handleCan1Frame(frame);
      return true;
    }

    Serial.printf("[AUTODETECT] No Ford response at %s\n", option.label);
    stopCan1();
  }

  Serial.println("[AUTODETECT] Falling back to 500 kbit/s assumption for Ford Ranger PX2");
  const bool started = installCan1Driver(BITRATES[0]);
  if (!started)
  {
    state.diagnosticBus = previousBus;
  }
  return started;
}

void initializeCan2()
{
  const DiagnosticBus previousBus = state.diagnosticBus;
  state.diagnosticBus = DiagnosticBus::Can2;

  for (const auto& option : BITRATES)
  {
    if (!installCan2Driver(option))
    {
      continue;
    }

    Serial.printf("[AUTODETECT] Listening for CAN2 traffic at %s\n", option.label);
    const unsigned long probeStart = millis();
    can_frame frame {};
    while (millis() - probeStart < 500)
    {
      if (can2Controller.readMessage(&frame) != MCP2515::ERROR_OK)
      {
        continue;
      }

      state.can2.status.detectedTraffic = true;
      handleCan2Frame(frame);
      return;
    }

    Serial.printf("[AUTODETECT] No CAN2 traffic at %s\n", option.label);
    stopCan2();
  }

  Serial.println("[AUTODETECT] CAN2 idle on tested bitrates; defaulting to passive 500 kbit/s monitor");
  installCan2Driver(BITRATES[0]);
  if (state.diagnosticBus == DiagnosticBus::Can2 && !state.can2.status.detectedTraffic)
  {
    state.diagnosticBus = previousBus;
  }
}

void setupRuntime()
{
  Serial.begin(115200);
  delay(400);
  if (GSERIAL_BRIDGE_ENABLED)
  {
    gscanBridgeSerial.begin(GSERIAL_BRIDGE_BAUD, SERIAL_8N1, GSERIAL_BRIDGE_RX_PIN, GSERIAL_BRIDGE_TX_PIN);
    bridgeCommandBuffer.reserve(40);
    Serial.printf(
      "[GS-BRIDGE] enabled baud=%lu RX=%d TX=%d\n",
      static_cast<unsigned long>(GSERIAL_BRIDGE_BAUD),
      GSERIAL_BRIDGE_RX_PIN,
      GSERIAL_BRIDGE_TX_PIN
    );
  }
  if (ESP_NOW_TELEMETRY_ENABLED)
  {
    const bool espNowOk = espNowManager.beginSender(ESP_NOW_DISPLAY_PEER_MAC, ESP_NOW_CHANNEL);
    const bool txTaskOk = espNowOk && espNowManager.startTxTask(50);
    Serial.printf("[ESPNOW] sender %s tx-task=%s\n", espNowOk ? "ready" : "failed", txTaskOk ? "running" : "failed");
  }
  uiStateInit(uiState);
  if (hasStatusLed())
  {
    pinMode(Pins::STATUS_LED, OUTPUT);
    digitalWrite(Pins::STATUS_LED, HIGH);
  }
  Serial.println();
  Serial.println("[SCANGAUGE3] Ford Ranger PX2 starter booting");
  printBoardProfile();
  if (!hasStatusLed())
  {
    Serial.println("[HEARTBEAT] LED blink disabled. Heartbeat activity is shown in serial logs.");
    Serial.println("[HEARTBEAT] This board profile does not expose a controllable status LED GPIO.");
  }
  printCommandMenu();
  syncUiBusStatus();
  uiStateSetStatus(uiState, "booting");
  waveshareUiEnabled = displayManager.begin(Serial);
  renderUiIfDirty();

  if (!initializeCan1())
  {
    Serial.println("[CANBUS] Unable to start CAN1 on tested bitrates");
    return;
  }

  initializeCan2();

  sendHeartbeat();
  state.lastHeartbeatMillis = millis();
  requestSupportedPidMaps();
  requestFordModuleAvailability();
  resetFordModuleDiagnosticSummary();
  sendVehicleInfoRequest(0x01);
  state.lastVehicleInfoMillis = millis();
  sendReadDtcRequest();
  state.lastDtcMillis = millis();
  sendNextPid();
  state.lastPidMillis = millis();
  renderUiIfDirty();
}

void loopRuntime()
{
  publishGscanBridgeStatus();
  pollGscanBridgeCommands();
  pollIncomingFrames();
  updateEspNowVehicleCache();

  if (!state.can1.status.started && !state.can2.status.started)
  {
    return;
  }

  const unsigned long now = millis();
  if (now - state.lastPidMillis >= PID_INTERVAL_MS)
  {
    sendNextPid();
    state.lastPidMillis = now;
  }

  if (now - state.lastHeartbeatMillis >= HEARTBEAT_INTERVAL_MS)
  {
    Serial.println("[FORD] Sending diagnostic heartbeat");
    sendHeartbeat();
    state.lastHeartbeatMillis = now;
  }

  if (now - state.lastVehicleInfoMillis >= VEHICLE_INFO_INTERVAL_MS)
  {
    sendVehicleInfoRequest(0x01);
    state.lastVehicleInfoMillis = now;
  }

  if (now - state.lastDtcMillis >= DTC_INTERVAL_MS)
  {
    runDiagnosticsFunction();
    state.lastDtcMillis = now;
  }

  if (now - state.lastStatusMillis >= STATUS_INTERVAL_MS)
  {
    printRuntimeStatus();
    if (hasStatusLed())
    {
      digitalWrite(Pins::STATUS_LED, !digitalRead(Pins::STATUS_LED));
    }
    else
    {
      Serial.println("[HEARTBEAT] tick");
    }
    state.lastStatusMillis = now;
  }

  if (state.fordSweepStartedMillis != 0 && now - state.fordSweepStartedMillis >= 1200)
  {
    printFordModuleAvailability();
    state.fordSweepStartedMillis = 0;
  }

  if (state.fordDiagSweepStartedMillis != 0 && now - state.fordDiagSweepStartedMillis >= 1500)
  {
    printFordModuleDiagnosticSummary();
    state.fordDiagSweepStartedMillis = 0;
  }

  if (state.clearWorkflowActive)
  {
    const unsigned long elapsed = now - state.clearWorkflowStartedMillis;
    if (state.clearWorkflowStage == 1 && elapsed >= 350)
    {
      sendReadStoredDtcRequest();
      sendReadPendingDtcRequest();
      sendReadPermanentDtcRequest();
      sendReadDtcRequest();
      state.clearWorkflowStage = 2;
    }
    else if (state.clearWorkflowStage == 2 && elapsed >= 1300)
    {
      printClearWorkflowSummary();
      printFordModuleDiagnosticSummary();
      state.clearWorkflowActive = false;
      state.clearWorkflowStage = 0;
      uiStateSetStatus(uiState, "Clear verify complete");
    }
  }

  pollSerialCommands();
  handleUiAction(displayManager.pollAction(Serial));
  renderUiIfDirty();

  delay(10);
}