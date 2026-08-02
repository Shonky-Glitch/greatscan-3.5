#include "app.h"

#include <Arduino.h>

#include "project_config.h"
#include "telemetry/telemetry_bridge.h"

namespace {
constexpr unsigned long kEspNowStaleMs = 500;
constexpr uint8_t kEspNowChannel = 1;

bool decodeScaledI16(const int16_t raw, const float scale, float& outValue) {
  if (raw == greatscan::kGaugeI16Unavailable || scale == 0.0f) {
    return false;
  }

  outValue = static_cast<float>(raw) / scale;
  return true;
}

void applyTelemetryToAppState(const greatscan::VehicleTelemetry& telemetry, float& rpm, float& coolantC, float& speedKph, float& boostKpa, float& fuelPct, float& throttlePct, float& dpfPct, float& oilPressureKpa, float& afr, float& oilTempC, float& voltage, float& atfC, float& eg3C, float& o2s1V, float& engineSpeedDidRpm) {
  if (!telemetry.valid) {
    return;
  }

  rpm = static_cast<float>(telemetry.rpm);
  coolantC = static_cast<float>(telemetry.coolantC);
  speedKph = static_cast<float>(telemetry.speedKph);
  boostKpa = static_cast<float>(telemetry.boostKpa);
  fuelPct = static_cast<float>(telemetry.fuelPct);
  throttlePct = static_cast<float>(telemetry.throttlePct);
  oilPressureKpa = static_cast<float>(telemetry.oilPressureKpa);
  afr = static_cast<float>(telemetry.afrX100) / 100.0f;
  oilTempC = static_cast<float>(telemetry.oilTempC);
  voltage = static_cast<float>(telemetry.batteryMv) / 1000.0f;
  atfC = static_cast<float>(telemetry.atfC);
  eg3C = static_cast<float>(telemetry.eg3C);
  o2s1V = static_cast<float>(telemetry.o2s1Mv) / 1000.0f;
  engineSpeedDidRpm = static_cast<float>(telemetry.rpm);
  dpfPct = 0.0f;
}
}

void App::pollUartTelemetry() {
  uartTransport_.tick();
  bootState_.markTransportHealthy(uartTransport_.isHealthy());

  if (!uartTransport_.hasFrame()) {
    return;
  }

  const greatscan::TelemetryFrame frame = uartTransport_.takeFrame();
  telemetryDecoder_.update(frame);
  if (!telemetryDecoder_.hasNewData()) {
    return;
  }

  const greatscan::VehicleTelemetry& telemetry = telemetryDecoder_.latest();
  applyTelemetryToAppState(telemetry, rpm_, coolantC_, speedKph_, boostKpa_, fuelPct_, throttlePct_, dpfPct_, oilPressureKpa_, afr_, oilTempC_, voltage_, atfC_, eg3C_, o2s1V_, engineSpeedDidRpm_);
  lastVehicleDataMs_ = millis();
  vehicleDataDisconnected_ = false;
  const greatscan::GaugePacket packet = greatscan::buildGaugePacketFromTelemetry(telemetry, static_cast<uint32_t>(millis()), millis());
  (void)espNowManager_.sendGaugePacket(packet);
  Serial.printf("[UART] rpm=%u speed=%u coolant=%d\n", telemetry.rpm, telemetry.speedKph, telemetry.coolantC);
}

void App::begin() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[BOOT] %s startup\n", ProjectConfig::kDeviceName);

  bootState_.begin();
  uartTransport_.begin(Serial1, 115200, 18, 17);
  telemetryDecoder_.begin();
  displayUi_.begin();
  bootState_.markUiReady(true);

  espNowModeEnabled_ = espNowManager_.beginReceiver(kEspNowChannel);
  if (espNowModeEnabled_) {
    Serial.println("[ESPNOW] receiver ready");
  } else {
    Serial.println("[ESPNOW] FAULT: receiver init failed, falling back to Wi-Fi mode");
  }

  if (!espNowModeEnabled_) {
    webUi_.begin(
        &rpm_,
        &coolantC_,
        &speedKph_,
        &boostKpa_,
        &fuelPct_,
        &throttlePct_,
        &dpfPct_,
        &oilPressureKpa_,
        &afr_,
        &oilTempC_,
        &voltage_,
        &atfC_,
        &eg3C_,
        &o2s1V_,
        &engineSpeedDidRpm_);
  }
}

void App::tick() {
  if (espNowModeEnabled_) {
    tickEspNowMode();
    return;
  }

  pollUartTelemetry();

  if (millis() - lastVehicleDataMs_ > 2000U && !vehicleDataDisconnected_) {
    vehicleDataDisconnected_ = true;
  }

  webUi_.tick();

  const DisplayUi::Action action = displayUi_.consumeAction();
  if (action == DisplayUi::Action::RefreshNow) {
    webUi_.requestImmediateRefresh();
  } else if (action == DisplayUi::Action::ModuleScan) {
    webUi_.runAutoModuleScan();
  } else if (action == DisplayUi::Action::ReadDtc) {
    webUi_.runAutoReadDtc();
  } else if (action == DisplayUi::Action::ClearDtc) {
    webUi_.runAutoClearDtc();
  }

  displayUi_.setLinkState(
      !webUi_.isRemoteOnline()
          ? (webUi_.isStationEnabled() && !webUi_.isStationConnected()
                 ? DisplayUi::LinkState::Rejoining
                 : DisplayUi::LinkState::Searching)
          : (webUi_.isTelemetryStale() ? DisplayUi::LinkState::Stale : DisplayUi::LinkState::Connected));
  displayUi_.setVehicleName(webUi_.detectedVehicleName());
  displayUi_.setDiagnosticResult(vehicleDataDisconnected_ ? "CAN-X2 disconnected" : webUi_.diagnosticSummary());
  displayUi_.setGaugeValues(rpm_, coolantC_);
  displayUi_.setAuxValues(speedKph_, boostKpa_, fuelPct_, throttlePct_, dpfPct_, oilPressureKpa_, afr_, oilTempC_, voltage_, atfC_, eg3C_, o2s1V_, engineSpeedDidRpm_);
  displayUi_.tick();
}

void App::tickEspNowMode() {
  espNowManager_.sendDiscoveryBeacon(1000);
  pollUartTelemetry();

  const DisplayUi::Action action = displayUi_.consumeAction();
  if (action == DisplayUi::Action::RefreshNow) {
    Serial.println("[ESPNOW] refresh requested");
  } else if (action == DisplayUi::Action::ModuleScan ||
             action == DisplayUi::Action::ReadDtc ||
             action == DisplayUi::Action::ClearDtc) {
    Serial.println("[ESPNOW] command unavailable in receiver-only mode");
  }

  greatscan::GaugePacket packet {};
  if (espNowManager_.popLatestPacket(packet)) {
    latestPacket_ = packet;
    rpm_ = static_cast<float>(packet.engineRpm);
    speedKph_ = static_cast<float>(packet.vehicleSpeedKphX10) / 10.0f;
    coolantC_ = static_cast<float>(packet.coolantTempCX10) / 10.0f;
    atfC_ = static_cast<float>(packet.transTempCX10) / 10.0f;
    oilTempC_ = static_cast<float>(packet.oilTempCX10) / 10.0f;
    boostKpa_ = static_cast<float>(packet.boostKpaX10) / 10.0f;
    voltage_ = static_cast<float>(packet.batteryMv) / 1000.0f;

    float decoded = 0.0f;
    fuelPct_ = decodeScaledI16(packet.fuelPctX10, 10.0f, decoded) ? decoded : 0.0f;
    throttlePct_ = decodeScaledI16(packet.throttlePctX10, 10.0f, decoded) ? decoded : 0.0f;
    dpfPct_ = decodeScaledI16(packet.dpfPctX10, 10.0f, decoded) ? decoded : 0.0f;
    oilPressureKpa_ = decodeScaledI16(packet.oilPressureKpaX10, 10.0f, decoded) ? decoded : 0.0f;
    afr_ = decodeScaledI16(packet.afrX100, 100.0f, decoded) ? decoded : 0.0f;
    eg3C_ = decodeScaledI16(packet.eg3TempCX10, 10.0f, decoded) ? decoded : 0.0f;
    o2s1V_ = decodeScaledI16(packet.o2s1Mv, 1000.0f, decoded) ? decoded : 0.0f;
    engineSpeedDidRpm_ = decodeScaledI16(packet.engineSpeedDidRpm, 1.0f, decoded) ? decoded : rpm_;
  }

  espNowRxFresh_ = espNowManager_.isLinkActive(kEspNowStaleMs);
  if (!espNowRxFresh_) {
    zeroDisplayedTelemetry();
  }

  if (millis() - lastVehicleDataMs_ > 2000U && !vehicleDataDisconnected_) {
    vehicleDataDisconnected_ = true;
  }

  displayUi_.setLinkState(
      espNowRxFresh_
          ? DisplayUi::LinkState::Connected
          : (espNowManager_.hasReceivedPacket() ? DisplayUi::LinkState::Stale : DisplayUi::LinkState::Searching));
  // Only claim "FORD" while we're actively receiving fresh, non-beacon
  // telemetry AND the scanner reports recent real CAN traffic (not just a
  // successful driver init). Otherwise fall back to "AUTO" -- covers both a
  // stale/disconnected ESP-NOW link and a powered-but-not-connected-to-a-
  // vehicle CAN-X2 scanner.
  const bool canOffline = (latestPacket_.statusFlags & greatscan::WarningCanOffline) != 0;
  displayUi_.setVehicleName(
      (espNowRxFresh_ && !canOffline && (latestPacket_.statusFlags & greatscan::kGaugeStatusFlagDiscoveryBeacon) == 0)
          ? "FORD"
          : "AUTO");
  displayUi_.setDiagnosticResult(vehicleDataDisconnected_ ? "CAN-X2 disconnected" : (espNowRxFresh_ ? "ESP-NOW link ok" : "ESP-NOW stale"));
  displayUi_.setGaugeValues(rpm_, coolantC_);
  displayUi_.setAuxValues(
      speedKph_,
      boostKpa_,
      fuelPct_,
      throttlePct_,
      dpfPct_,
      oilPressureKpa_,
      afr_,
      oilTempC_,
      voltage_,
      atfC_,
      eg3C_,
      o2s1V_,
      engineSpeedDidRpm_);
  displayUi_.tick();
}

void App::zeroDisplayedTelemetry() {
  rpm_ = 0.0f;
  coolantC_ = 0.0f;
  speedKph_ = 0.0f;
  boostKpa_ = 0.0f;
  fuelPct_ = 0.0f;
  throttlePct_ = 0.0f;
  dpfPct_ = 0.0f;
  oilPressureKpa_ = 0.0f;
  afr_ = 0.0f;
  oilTempC_ = 0.0f;
  voltage_ = 0.0f;
  atfC_ = 0.0f;
  eg3C_ = 0.0f;
  o2s1V_ = 0.0f;
  engineSpeedDidRpm_ = 0.0f;
}
