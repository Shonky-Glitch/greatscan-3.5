#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <gauge_packet.h>

namespace greatscan {

struct VehicleDataCache {
  uint16_t engineRpm = 0;
  int16_t vehicleSpeedKphX10 = 0;
  int16_t coolantTempCX10 = 0;
  int16_t transTempCX10 = 0;
  int16_t oilTempCX10 = 0;
  int16_t boostKpaX10 = 0;
  uint16_t batteryMv = 0;
  uint8_t selectedGear = static_cast<uint8_t>(GearState::Unknown);
  uint32_t statusFlags = WarningNone;
  int16_t fuelPctX10 = kGaugeI16Unavailable;
  int16_t throttlePctX10 = kGaugeI16Unavailable;
  int16_t dpfPctX10 = kGaugeI16Unavailable;
  int16_t oilPressureKpaX10 = kGaugeI16Unavailable;
  int16_t afrX100 = kGaugeI16Unavailable;
  int16_t eg3TempCX10 = kGaugeI16Unavailable;
  int16_t o2s1Mv = kGaugeI16Unavailable;
  int16_t engineSpeedDidRpm = kGaugeI16Unavailable;
};

class ESPNowManager {
 public:
  bool beginSender(const uint8_t peerMac[6], uint8_t wifiChannel = 1, bool autoDiscovery = true);
  bool updateVehicleCache(const VehicleDataCache& cache);
  bool startTxTask(uint32_t periodMs = 50, UBaseType_t priority = 1, uint16_t stackDepth = 4096);
  void stopTxTask();
  bool txTaskRunning() const { return txTaskHandle_ != nullptr; }
  bool isReady() const { return ready_; }
  bool usingDiscoveredPeer() const { return discoveredPeerValid_; }
  uint32_t txOkCount() const { return txOkCount_; }
  uint32_t txFailCount() const { return txFailCount_; }
  const uint8_t* currentPeerMac() const { return peerMac_; }
  uint32_t sequence() const { return sequence_; }
  uint32_t heartbeatCounter() const { return heartbeatCounter_; }

 private:
  static void txTaskEntry(void* context);
  void txTaskLoop();
  bool sendLatestPacket();
  static void onSend(const uint8_t* macAddr, esp_now_send_status_t status);
  static void onReceive(const uint8_t* macAddr, const uint8_t* data, int len);
  bool addPeer(const uint8_t peerMac[6], uint8_t wifiChannel);
  void adoptDiscoveredPeer(const uint8_t peerMac[6]);
  void printTxStatus(unsigned long nowMs);

  static ESPNowManager* instance_;
  bool initialized_ = false;
  bool ready_ = false;
  bool autoDiscovery_ = true;
  bool preferredPeerValid_ = false;
  bool discoveredPeerValid_ = false;
  uint8_t wifiChannel_ = 1;
  uint8_t peerMac_[6] = {0};
  uint8_t preferredPeerMac_[6] = {0};
  VehicleDataCache cache_ {};
  mutable portMUX_TYPE cacheMux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t txTaskHandle_ = nullptr;
  uint32_t txPeriodMs_ = 50;
  uint32_t sequence_ = 0;
  uint32_t heartbeatCounter_ = 0;
  unsigned long lastStatusPrintMs_ = 0;
  uint32_t txOkCount_ = 0;
  uint32_t txFailCount_ = 0;
};

} // namespace greatscan
