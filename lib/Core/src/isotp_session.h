#pragma once

#include <cstddef>
#include <cstdint>

struct IsoTpSession
{
  bool active = false;
  uint16_t totalLength = 0;
  uint8_t service = 0;
  uint8_t infoType = 0;
  size_t bytesCollected = 0;
  uint8_t nextSequence = 1;
  uint32_t sourceId = 0;
  uint8_t buffer[32] = {};
};

enum class IsoTpUpdate : uint8_t
{
  Ignored,
  Started,
  Continued,
  Complete,
};

void resetIsoTpSession(IsoTpSession& session);
size_t isoTpPayloadLength(const IsoTpSession& session);
IsoTpUpdate handleIsoTpFirstFrame(IsoTpSession& session, uint32_t sourceId, const uint8_t* data, size_t dataLength);
IsoTpUpdate handleIsoTpConsecutiveFrame(IsoTpSession& session, uint32_t sourceId, const uint8_t* data, size_t dataLength);