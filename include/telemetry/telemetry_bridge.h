#pragma once

#include <gauge_packet.h>

#include "telemetry/vehicle_data.h"

namespace greatscan {

GaugePacket buildGaugePacketFromTelemetry(const VehicleTelemetry& telemetry, uint32_t sequence, uint32_t timestampMs);

}  // namespace greatscan
