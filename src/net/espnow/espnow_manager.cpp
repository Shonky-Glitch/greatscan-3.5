#include "espnow_manager.h"

#include <cstring>

#include <esp_wifi.h>

namespace greatscan {

namespace {
constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

ESPNowManager* ESPNowManager::instance_ = nullptr;

bool ESPNowManager::beginReceiver(const uint8_t wifiChannel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (wifiChannel > 0) {
    esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
  }

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  instance_ = this;
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t broadcastPeer {};
  std::memcpy(broadcastPeer.peer_addr, kBroadcastMac, sizeof(kBroadcastMac));
  broadcastPeer.channel = 0;
  broadcastPeer.encrypt = false;
  if (!esp_now_is_peer_exist(kBroadcastMac)) {
    esp_now_add_peer(&broadcastPeer);
  }
  return true;
}

bool ESPNowManager::sendDiscoveryBeacon(const unsigned long minIntervalMs) {
  const unsigned long now = millis();
  if (now - lastDiscoveryBeaconMs_ < minIntervalMs) {
    return false;
  }

  GaugePacket beacon {};
  beacon.protocolVersion = kGaugePacketProtocolVersion;
  beacon.sequence = ++discoverySequence_;
  beacon.timestampMs = now;
  beacon.statusFlags = kGaugeStatusFlagDiscoveryBeacon;
  beacon.heartbeatCounter = discoverySequence_;

  lastDiscoveryBeaconMs_ = now;
  return esp_now_send(kBroadcastMac, reinterpret_cast<const uint8_t*>(&beacon), sizeof(beacon)) == ESP_OK;
}

bool ESPNowManager::sendGaugePacket(const GaugePacket& packet) {
  lastSentSequence_ = packet.sequence;
  return esp_now_send(kBroadcastMac, reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) == ESP_OK;
}

bool ESPNowManager::relayGaugePacket(const GaugePacket& packet) {
  if (packet.sequence == 0 || packet.sequence == lastSentSequence_ || packet.sequence == lastRelayedSequence_) {
    return false;
  }

  lastRelayedSequence_ = packet.sequence;
  return sendGaugePacket(packet);
}

bool ESPNowManager::hasFreshPacket(const unsigned long staleMs) const {
  if (!hasPacket_) {
    return false;
  }
  return (millis() - lastRxMs_) <= staleMs;
}

bool ESPNowManager::popLatestPacket(GaugePacket& outPacket) {
  return readLatestPacket(outPacket);
}

bool ESPNowManager::readLatestPacket(GaugePacket& outPacket) const {
  bool hasPacket = false;
  portENTER_CRITICAL(&cacheMux_);
  hasPacket = hasPacket_;
  if (hasPacket) {
    outPacket = latest_;
  }
  portEXIT_CRITICAL(&cacheMux_);
  return hasPacket;
}

bool ESPNowManager::isLinkActive(const unsigned long timeoutMs) const {
  unsigned long lastValidMs = 0;
  bool hasPacket = false;
  portENTER_CRITICAL(&cacheMux_);
  hasPacket = hasPacket_;
  lastValidMs = lastValidPacketMs_;
  portEXIT_CRITICAL(&cacheMux_);

  if (!hasPacket) {
    return false;
  }
  return (millis() - lastValidMs) <= timeoutMs;
}

bool ESPNowManager::isLinkLost(const unsigned long timeoutMs) const {
  return !isLinkActive(timeoutMs);
}

bool ESPNowManager::hasReceivedPacket() const {
  bool hasPacket = false;
  portENTER_CRITICAL(&cacheMux_);
  hasPacket = hasPacket_;
  portEXIT_CRITICAL(&cacheMux_);
  return hasPacket;
}

unsigned long ESPNowManager::lastValidPacketMs() const {
  unsigned long value = 0;
  portENTER_CRITICAL(&cacheMux_);
  value = lastValidPacketMs_;
  portEXIT_CRITICAL(&cacheMux_);
  return value;
}

uint32_t ESPNowManager::lastSequence() const {
  uint32_t value = 0;
  portENTER_CRITICAL(&cacheMux_);
  value = lastSequence_;
  portEXIT_CRITICAL(&cacheMux_);
  return value;
}

uint32_t ESPNowManager::missedPacketCount() const {
  uint32_t value = 0;
  portENTER_CRITICAL(&cacheMux_);
  value = missedPacketCount_;
  portEXIT_CRITICAL(&cacheMux_);
  return value;
}

void ESPNowManager::onReceive(const uint8_t* /*macAddr*/, const uint8_t* data, const int len) {
  if (!instance_ || !data || len != static_cast<int>(sizeof(GaugePacket))) {
    if (instance_) {
      ++instance_->rxDropCount_;
    }
    return;
  }

  GaugePacket packet {};
  std::memcpy(&packet, data, sizeof(packet));
  if (packet.protocolVersion != kGaugePacketProtocolVersion) {
    ++instance_->rxDropCount_;
    return;
  }

  const unsigned long now = millis();
  portENTER_CRITICAL(&instance_->cacheMux_);
  if (instance_->hasSequence_) {
    const uint32_t delta = packet.sequence - instance_->lastSequence_;
    if (delta > 1U) {
      instance_->missedPacketCount_ += (delta - 1U);
    }
  }
  instance_->lastSequence_ = packet.sequence;
  instance_->hasSequence_ = true;
  instance_->latest_ = packet;
  instance_->hasPacket_ = true;
  instance_->lastValidPacketMs_ = now;
  portEXIT_CRITICAL(&instance_->cacheMux_);

  instance_->lastRxMs_ = now;
  ++instance_->rxOkCount_;

  if (packet.protocolVersion == kGaugePacketProtocolVersion && packet.sequence != 0) {
    (void)instance_->relayGaugePacket(packet);
  }
}

} // namespace greatscan
