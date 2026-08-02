#pragma once

#include <cstddef>
#include <cstdint>

struct FordModule
{
	uint32_t requestId;
	uint32_t responseId;
	const char* name;
};

namespace FordIds
{
constexpr uint32_t BROADCAST_REQUEST = 0x7DF;
constexpr uint32_t ECU_RESPONSE_LOW = 0x7E8;
constexpr uint32_t ECU_RESPONSE_HIGH = 0x7EF;
}

namespace FordServices
{
constexpr uint8_t SHOW_CURRENT_DATA = 0x01;
constexpr uint8_t READ_STORED_DTC = 0x03;
constexpr uint8_t READ_PENDING_DTC = 0x07;
constexpr uint8_t READ_PERMANENT_DTC = 0x0A;
constexpr uint8_t REQUEST_VEHICLE_INFO = 0x09;
constexpr uint8_t READ_DTC_BY_STATUS = 0x19;
constexpr uint8_t FLOW_CONTROL = 0x30;
constexpr uint8_t DIAGNOSTIC_SESSION_CONTROL = 0x10;
constexpr uint8_t CURRENT_DATA_RESPONSE = 0x41;
constexpr uint8_t STORED_DTC_RESPONSE = 0x43;
constexpr uint8_t PENDING_DTC_RESPONSE = 0x47;
constexpr uint8_t PERMANENT_DTC_RESPONSE = 0x4A;
constexpr uint8_t VEHICLE_INFO_RESPONSE = 0x49;
constexpr uint8_t SESSION_CONTROL_RESPONSE = 0x50;
constexpr uint8_t READ_DTC_POSITIVE_RESPONSE = 0x59;
}

namespace FordPids
{
constexpr uint8_t SUPPORTED_01_20 = 0x00;
constexpr uint8_t SUPPORTED_21_40 = 0x20;
constexpr uint8_t SUPPORTED_41_60 = 0x40;
constexpr uint8_t SUPPORTED_61_80 = 0x60;

constexpr uint8_t ENGINE_LOAD = 0x04;
constexpr uint8_t SHORT_TERM_FUEL_TRIM_B1 = 0x06;
constexpr uint8_t LONG_TERM_FUEL_TRIM_B1 = 0x07;
constexpr uint8_t FUEL_PRESSURE = 0x0A;
constexpr uint8_t INTAKE_MAP = 0x0B;
constexpr uint8_t ENGINE_SPEED = 0x0C;
constexpr uint8_t VEHICLE_SPEED = 0x0D;
constexpr uint8_t TIMING_ADVANCE = 0x0E;
constexpr uint8_t INTAKE_AIR_TEMP = 0x0F;
constexpr uint8_t MAF_RATE = 0x10;
constexpr uint8_t COOLANT_TEMP = 0x05;
constexpr uint8_t O2_SENSOR_1 = 0x14;
constexpr uint8_t RUN_TIME_SINCE_START = 0x1F;
constexpr uint8_t FUEL_LEVEL = 0x2F;
constexpr uint8_t DISTANCE_SINCE_DTC_CLEAR = 0x31;
constexpr uint8_t CONTROL_MODULE_VOLTAGE = 0x42;
constexpr uint8_t AMBIENT_AIR_TEMP = 0x46;
constexpr uint8_t ENGINE_OIL_TEMP = 0x5C;
constexpr uint8_t THROTTLE_POSITION = 0x11;
}

const char* pidName(uint8_t pid);
const char* infoTypeName(uint8_t infoType);
const char* nrcName(uint8_t nrc);
bool isFordResponse(uint32_t canId);
bool formatPidValue(uint8_t pid, const uint8_t* value, size_t valueLength, char* output, size_t outputSize);
bool formatVehicleInfoValue(uint8_t infoType, const uint8_t* value, size_t valueLength, char* output, size_t outputSize);
bool decodeDtcBytes(uint8_t first, uint8_t second, char* output, size_t outputSize);
bool decodeUdsDtcBytes(uint8_t first, uint8_t second, uint8_t third, char* output, size_t outputSize);
size_t fordModuleCount();
const FordModule* fordModuleByIndex(size_t index);
const char* fordModuleNameByResponseId(uint32_t responseId);
int fordModuleIndexByResponseId(uint32_t responseId);