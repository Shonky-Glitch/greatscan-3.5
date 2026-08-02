#pragma once

#include <gauge_packet.h>

#include "comm/boot_state.h"
#include "comm/uart_transport.h"
#include "net/espnow/espnow_manager.h"
#include "net/web_ui.h"
#include "telemetry/telemetry_decoder.h"
#include "ui/display_ui.h"

class App {
 public:
  void begin();
  void tick();

 private:
  void tickEspNowMode();
  void zeroDisplayedTelemetry();
  void pollUartTelemetry();

  DisplayUi displayUi_;
  WebUi webUi_;
  greatscan::BootState bootState_;
  greatscan::UartTransport uartTransport_;
  greatscan::TelemetryDecoder telemetryDecoder_;
  greatscan::ESPNowManager espNowManager_;
  greatscan::GaugePacket latestPacket_ {};
  bool espNowModeEnabled_ = true;
  bool espNowRxFresh_ = false;
  unsigned long lastVehicleDataMs_ = 0;
  bool vehicleDataDisconnected_ = false;
  float rpm_ = 750.0f;
  float coolantC_ = 80.0f;
  float speedKph_ = 0.0f;
  float boostKpa_ = 0.0f;
  float fuelPct_ = 0.0f;
  float throttlePct_ = 0.0f;
  float dpfPct_ = 0.0f;
  float oilPressureKpa_ = 0.0f;
  float afr_ = 14.7f;
  float oilTempC_ = 0.0f;
  float voltage_ = 12.0f;
  float atfC_ = 0.0f;
  float eg3C_ = 0.0f;
  float o2s1V_ = 0.0f;
  float engineSpeedDidRpm_ = 0.0f;
};
