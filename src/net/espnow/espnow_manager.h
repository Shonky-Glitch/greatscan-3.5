#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <WiFi.h>

#include <gauge_packet.h>

namespace greatscan {

class ESPNowManager {
 public:
  bool beginReceiver(uint8_t wifiChannel = 1);
  bool sendDiscoveryBeacon(unsigned long minIntervalMs = 1000);
  bool sendGaugePacket(const GaugePacket& packet);
  bool relayGaugePacket(const GaugePacket& packet);
  bool hasFreshPacket(unsigned long staleMs) const;
  bool popLatestPacket(GaugePacket& outPacket);
  bool readLatestPacket(GaugePacket& outPacket) const;
  bool isLinkActive(unsigned long timeoutMs = 500) const;
  bool isLinkLost(unsigned long timeoutMs = 500) const;
  bool hasReceivedPacket() const;
  unsigned long lastValidPacketMs() const;
  uint32_t lastSequence() const;
  uint32_t missedPacketCount() const;

  uint32_t rxOkCount() const { return rxOkCount_; }
  uint32_t rxDropCount() const { return rxDropCount_; }
  unsigned long lastRxMs() const { return lastRxMs_; }

 private:
  static void onReceive(const uint8_t* macAddr, const uint8_t* data, int len);

  static ESPNowManager* instance_;
  GaugePacket latest_ {};
  bool hasPacket_ = false;
  bool hasSequence_ = false;
  uint32_t lastSequence_ = 0;
  uint32_t missedPacketCount_ = 0;
  unsigned long lastValidPacketMs_ = 0;
  mutable portMUX_TYPE cacheMux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t rxOkCount_ = 0;
  volatile uint32_t rxDropCount_ = 0;
  volatile unsigned long lastRxMs_ = 0;
  unsigned long lastDiscoveryBeaconMs_ = 0;
  uint32_t discoverySequence_ = 0;
  uint32_t lastSentSequence_ = 0;
  uint32_t lastRelayedSequence_ = 0;
};

} // namespace greatscan
