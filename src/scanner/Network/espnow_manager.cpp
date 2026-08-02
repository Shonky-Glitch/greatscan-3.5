#include "espnow_manager.h"

#include <cstring>

namespace {
constexpr uint32_t kTxStatusPrintIntervalMs = 1000;
}

namespace greatscan {

ESPNowManager* ESPNowManager::instance_ = nullptr;

bool ESPNowManager::beginSender(const uint8_t peerMac[6], const uint8_t wifiChannel, const bool autoDiscovery) {
  if (!peerMac) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  instance_ = this;
  initialized_ = true;
  autoDiscovery_ = autoDiscovery;
  wifiChannel_ = wifiChannel;
  esp_now_register_send_cb(onSend);
  esp_now_register_recv_cb(onReceive);

  std::memcpy(preferredPeerMac_, peerMac, sizeof(preferredPeerMac_));
  preferredPeerValid_ = true;
  std::memcpy(peerMac_, peerMac, sizeof(peerMac_));
  ready_ = addPeer(peerMac_, wifiChannel_);
  discoveredPeerValid_ = false;
  return initialized_;
}

bool ESPNowManager::updateVehicleCache(const VehicleDataCache& cache) {
  portENTER_CRITICAL(&cacheMux_);
  cache_ = cache;
  portEXIT_CRITICAL(&cacheMux_);
  return true;
}

bool ESPNowManager::startTxTask(const uint32_t periodMs, const UBaseType_t priority, const uint16_t stackDepth) {
  if (!initialized_ || txTaskHandle_ != nullptr) {
    return txTaskHandle_ != nullptr;
  }

  txPeriodMs_ = (periodMs == 0) ? 50 : periodMs;
  BaseType_t result = xTaskCreate(
      txTaskEntry,
      "espnow_tx",
      stackDepth,
      this,
      priority,
      &txTaskHandle_);
  return result == pdPASS;
}

void ESPNowManager::stopTxTask() {
  if (txTaskHandle_ == nullptr) {
    return;
  }
  TaskHandle_t task = txTaskHandle_;
  txTaskHandle_ = nullptr;
  vTaskDelete(task);
}

void ESPNowManager::txTaskEntry(void* context) {
  ESPNowManager* manager = static_cast<ESPNowManager*>(context);
  if (!manager) {
    vTaskDelete(nullptr);
    return;
  }
  manager->txTaskLoop();
}

void ESPNowManager::txTaskLoop() {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t periodTicks = pdMS_TO_TICKS(txPeriodMs_);

  while (true) {
    sendLatestPacket();
    vTaskDelayUntil(&lastWake, periodTicks);
  }
}

bool ESPNowManager::addPeer(const uint8_t peerMac[6], const uint8_t wifiChannel) {
  esp_now_peer_info_t peerInfo {};
  std::memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = wifiChannel;
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(peerMac)) {
    return true;
  }

  return esp_now_add_peer(&peerInfo) == ESP_OK;
}

bool ESPNowManager::sendLatestPacket() {
  if (!initialized_ || !ready_) {
    return false;
  }

  VehicleDataCache snapshot {};
  portENTER_CRITICAL(&cacheMux_);
  snapshot = cache_;
  portEXIT_CRITICAL(&cacheMux_);

  GaugePacket tx {};
  tx.protocolVersion = kGaugePacketProtocolVersion;
  tx.sequence = ++sequence_;
  tx.timestampMs = millis();
  tx.engineRpm = snapshot.engineRpm;
  tx.vehicleSpeedKphX10 = snapshot.vehicleSpeedKphX10;
  tx.coolantTempCX10 = snapshot.coolantTempCX10;
  tx.transTempCX10 = snapshot.transTempCX10;
  tx.oilTempCX10 = snapshot.oilTempCX10;
  tx.boostKpaX10 = snapshot.boostKpaX10;
  tx.batteryMv = snapshot.batteryMv;
  tx.selectedGear = snapshot.selectedGear;
  tx.statusFlags = snapshot.statusFlags;
  tx.heartbeatCounter = ++heartbeatCounter_;
  tx.fuelPctX10 = snapshot.fuelPctX10;
  tx.throttlePctX10 = snapshot.throttlePctX10;
  tx.dpfPctX10 = snapshot.dpfPctX10;
  tx.oilPressureKpaX10 = snapshot.oilPressureKpaX10;
  tx.afrX100 = snapshot.afrX100;
  tx.eg3TempCX10 = snapshot.eg3TempCX10;
  tx.o2s1Mv = snapshot.o2s1Mv;
  tx.engineSpeedDidRpm = snapshot.engineSpeedDidRpm;

  const unsigned long now = tx.timestampMs;
  printTxStatus(now);
  return esp_now_send(peerMac_, reinterpret_cast<const uint8_t*>(&tx), sizeof(tx)) == ESP_OK;
}

void ESPNowManager::onSend(const uint8_t* /*macAddr*/, const esp_now_send_status_t status) {
  if (!instance_) {
    return;
  }
  if (status == ESP_NOW_SEND_SUCCESS) {
    ++instance_->txOkCount_;
  } else {
    ++instance_->txFailCount_;
  }
}

void ESPNowManager::onReceive(const uint8_t* macAddr, const uint8_t* data, const int len) {
  if (!instance_ || !macAddr || !data || len != static_cast<int>(sizeof(GaugePacket))) {
    return;
  }

  GaugePacket packet {};
  std::memcpy(&packet, data, sizeof(packet));
  if (packet.protocolVersion != kGaugePacketProtocolVersion) {
    return;
  }

  if (instance_->autoDiscovery_ && (packet.statusFlags & kGaugeStatusFlagDiscoveryBeacon) != 0) {
    instance_->adoptDiscoveredPeer(macAddr);
  }
}

void ESPNowManager::adoptDiscoveredPeer(const uint8_t peerMac[6]) {
  if (!peerMac) {
    return;
  }

  if (!addPeer(peerMac, wifiChannel_)) {
    return;
  }

  std::memcpy(peerMac_, peerMac, sizeof(peerMac_));
  discoveredPeerValid_ = true;
  ready_ = true;
}

void ESPNowManager::printTxStatus(const unsigned long nowMs) {
  if (nowMs - lastStatusPrintMs_ < kTxStatusPrintIntervalMs) {
    return;
  }

  lastStatusPrintMs_ = nowMs;
  Serial.printf(
      "[ESPNOW] tx peer=%02X:%02X:%02X:%02X:%02X:%02X seq=%lu hb=%lu ok=%lu fail=%lu source=%s\n",
      peerMac_[0],
      peerMac_[1],
      peerMac_[2],
      peerMac_[3],
      peerMac_[4],
      peerMac_[5],
      static_cast<unsigned long>(sequence_),
      static_cast<unsigned long>(heartbeatCounter_),
      static_cast<unsigned long>(txOkCount_),
      static_cast<unsigned long>(txFailCount_),
      discoveredPeerValid_ ? "discovered" : "preferred");
}

} // namespace greatscan
