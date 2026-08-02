#pragma once

#include <Arduino.h>

class WebUi {
 public:
  void begin(
      float* rpmValue,
      float* coolantValue,
      float* speedValue,
      float* boostValue,
      float* fuelValue,
      float* throttleValue,
      float* dpfValue,
      float* oilPressureValue,
      float* afrValue,
      float* oilTempValue,
      float* voltageValue,
      float* atfValue,
      float* eg3Value,
      float* o2s1Value,
      float* engineSpeedDidValue);
  void tick();
  void requestImmediateRefresh();
  bool runAutoModuleScan();
  bool runAutoReadDtc();
  bool runAutoClearDtc();
  bool isRemoteOnline() const;
  bool isTelemetryStale() const;
  bool isStationEnabled() const;
  bool isStationConnected() const;
  const char* detectedVehicleName() const;
  const char* lastCommandResult() const;
  const char* diagnosticSummary() const;

 private:
  float* rpm_ = nullptr;
  float* coolantC_ = nullptr;
  float* speedKph_ = nullptr;
  float* boostKpa_ = nullptr;
  float* fuelPct_ = nullptr;
  float* throttlePct_ = nullptr;
  float* dpfPct_ = nullptr;
  float* oilPressureKpa_ = nullptr;
  float* afr_ = nullptr;
  float* oilTempC_ = nullptr;
  float* voltage_ = nullptr;
  float* atfC_ = nullptr;
  float* eg3C_ = nullptr;
  float* o2s1V_ = nullptr;
  float* engineSpeedDidRpm_ = nullptr;
  unsigned long lastRemotePollMs_ = 0;
  unsigned long lastRemoteRawPollMs_ = 0;
  bool remoteOnline_ = false;
  String remoteDiag_ = "-";
  String remoteCan1_ = "-";
  String remoteCan2_ = "-";
  unsigned long lastCan1UpdateMs_ = 0;
  unsigned long lastCan2UpdateMs_ = 0;
  String remoteVin_ = "-";
  int remoteDtcCount_ = 0;
  int remoteDtcStored_ = 0;
  int remoteDtcPending_ = 0;
  int remoteDtcPermanent_ = 0;
  String remoteLastStored_ = "-";
  String remoteLastPending_ = "-";
  String remoteLastPermanent_ = "-";
  String remoteUdsSummary_ = "idle";
  int remoteFordModuleMask_ = 0;
  int remoteToyotaModuleMask_ = 0;
  bool remoteSpeedValid_ = false;
  bool remoteBoostValid_ = false;
  bool remoteFuelValid_ = false;
  bool remoteThrottleValid_ = false;
  bool remoteDpfValid_ = false;
  bool remoteOilPressureValid_ = false;
  bool remoteAfrValid_ = false;
  bool remoteOilTempValid_ = false;
  bool remoteVoltageValid_ = false;
  bool remoteAtfValid_ = false;
  bool remoteEg3Valid_ = false;
  bool remoteO2s1Valid_ = false;
  bool remoteEngineSpeedDidValid_ = false;
  float remoteSpeedKph_ = 0.0f;
  float remoteBoostKpa_ = 0.0f;
  float remoteFuelPct_ = 0.0f;
  float remoteThrottlePct_ = 0.0f;
  float remoteDpfPct_ = 0.0f;
  float remoteOilPressureKpa_ = 0.0f;
  float remoteAfr_ = 14.7f;
  float remoteOilTempC_ = 0.0f;
  float remoteVoltage_ = 0.0f;
  float remoteAtfC_ = 0.0f;
  float remoteEg3C_ = 0.0f;
  float remoteO2s1V_ = 0.0f;
  float remoteEngineSpeedDidRpm_ = 0.0f;
  unsigned long remoteRawSeq_ = 0;
  unsigned long lastLoggedRawSeq_ = 0;
  String remoteRawLine_ = "-";
  String lastCommandResult_ = "idle";
  String detectedVehicle_ = "AUTO";
  unsigned long lastRemoteStatusOkMs_ = 0;
  unsigned long lastRemoteGaugeMs_ = 0;
  bool remoteGaugeValid_ = false;
  bool pollStatusNext_ = true;
  bool stationEnabled_ = false;
  bool stationConnected_ = false;
  bool stationConnectedLogged_ = false;
  unsigned long lastStationReconnectMs_ = 0;
  uint8_t statusPollFailureCount_ = 0;
  uint8_t rawPollFailureCount_ = 0;
  unsigned long statusPollBackoffUntilMs_ = 0;
  unsigned long rawPollBackoffUntilMs_ = 0;

  void pollGreatScanStatus();
  void pollGreatScanRaw();
  void pollGreatScanSerial();
  void maintainStationConnection();
  void zeroAllGaugeValues();
  bool sendGreatScanCommand(char command, String& responseBody);
  bool parseGreatScanStatus(const String& payload);
  bool parseGreatScanRaw(const String& payload);
  bool parseGaugeValuesFromStatus(const String& payload);
  bool parseGaugeValuesFromRawLine(const String& line);
  void refreshDiagnosticSummaryFromStatus();
  void refreshDiagnosticSummaryFromRawLine(const String& line);
  void updateDetectedVehicle();

  String serialLineBuffer_;
  unsigned long lastSerialPacketMs_ = 0;
  unsigned long serialBridgeFrameCount_ = 0;
};
