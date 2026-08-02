#include "isotp_session.h"

#include <cstring>

void resetIsoTpSession(IsoTpSession& session)
{
  session = {};
}

size_t isoTpPayloadLength(const IsoTpSession& session)
{
  return session.totalLength > 2 ? session.totalLength - 2 : 0;
}

IsoTpUpdate handleIsoTpFirstFrame(
  IsoTpSession& session,
  const uint32_t sourceId,
  const uint8_t* data,
  const size_t dataLength
)
{
  if (!data || dataLength < 8 || (data[0] >> 4) != 0x1)
  {
    return IsoTpUpdate::Ignored;
  }

  resetIsoTpSession(session);
  session.active = true;
  session.totalLength = static_cast<uint16_t>((data[0] & 0x0F) << 8) | data[1];
  session.service = data[2];
  session.infoType = data[3];
  session.sourceId = sourceId;

  const size_t payloadLength = isoTpPayloadLength(session);
  const size_t chunkLength = payloadLength < 4 ? payloadLength : 4;
  std::memcpy(session.buffer, &data[4], chunkLength);
  session.bytesCollected = chunkLength;

  return session.bytesCollected >= payloadLength ? IsoTpUpdate::Complete : IsoTpUpdate::Started;
}

IsoTpUpdate handleIsoTpConsecutiveFrame(
  IsoTpSession& session,
  const uint32_t sourceId,
  const uint8_t* data,
  const size_t dataLength
)
{
  if (!data || !session.active || sourceId != session.sourceId || dataLength < 2)
  {
    return IsoTpUpdate::Ignored;
  }

  if ((data[0] >> 4) != 0x2 || (data[0] & 0x0F) != session.nextSequence)
  {
    return IsoTpUpdate::Ignored;
  }

  const size_t payloadLength = isoTpPayloadLength(session);
  const size_t remaining = payloadLength > session.bytesCollected ? payloadLength - session.bytesCollected : 0;
  const size_t available = dataLength - 1;
  const size_t chunkLength = remaining < available ? remaining : available;
  std::memcpy(&session.buffer[session.bytesCollected], &data[1], chunkLength);
  session.bytesCollected += chunkLength;
  session.nextSequence = static_cast<uint8_t>((session.nextSequence + 1) & 0x0F);

  return session.bytesCollected >= payloadLength ? IsoTpUpdate::Complete : IsoTpUpdate::Continued;
}