#pragma once

#include <stdint.h>

class DisplayUi {
 public:
  enum class Action : uint8_t {
    None = 0,
    RefreshNow,
    ModuleScan,
    ReadDtc,
    ClearDtc,
  };

  // Searching: no upstream link established yet (no packet/response ever seen).
  // Connected: fresh, valid data received within the activity timeout.
  // Stale: link was established but no valid data within the activity timeout.
  // Rejoining: link dropped and an active reconnect attempt is underway.
  // Fault: transport failed to initialise.
  enum class LinkState : uint8_t {
    Searching = 0,
    Connected,
    Stale,
    Rejoining,
    Fault,
  };

  void begin();
  void tick();
  void setGaugeValues(float rpm, float coolantC);
  void setAuxValues(
      float speedKph,
      float boostKpa,
      float fuelPct,
      float throttlePct,
      float dpfPct,
      float oilPressureKpa,
      float afr,
      float oilTempC,
      float voltage,
      float atfC,
      float eg3C,
      float o2s1V,
      float engineSpeedDidRpm);
  void setDiagnosticResult(const char* status);
  void setLinkState(LinkState state);
  void setVehicleName(const char* name);
  Action consumeAction();

 private:
  struct TouchPoint {
    int x = 0;
    int y = 0;
  };

  bool readTouchPoint(TouchPoint& point);
  void pollTouch();
  void handleTouchTap(const TouchPoint& point);
  void handleTouchRelease(const TouchPoint& point);
  void cyclePage(int step);
  void showActivePage();
  void drawTouchDiagnostics(bool force);

  bool screenReady_ = false;
  float lastRpm_ = 0.0f;
  float lastCoolantC_ = 0.0f;
  float lastSpeedKph_ = 0.0f;
  float lastBoostKpa_ = 0.0f;
  float lastFuelPct_ = 0.0f;
  float lastThrottlePct_ = 0.0f;
  float lastDpfPct_ = 0.0f;
  float lastOilPressureKpa_ = 0.0f;
  float lastAfr_ = 14.7f;
  float lastOilTempC_ = 0.0f;
  float lastVoltage_ = 0.0f;
  unsigned long lastPrintMs_ = 0;
  unsigned long lastTouchPollMs_ = 0;
  unsigned long touchDownStartMs_ = 0;
  bool touchControllerReady_ = false;
  bool touchWasDown_ = false;
  bool touchTracking_ = false;
  bool touchHoldCustomizeTriggered_ = false;
  bool showTouchDiagnostics_ = false;
  uint8_t pageIndex_ = 0;
  unsigned long lastTouchActionMs_ = 0;
  Action pendingAction_ = Action::None;
  TouchPoint touchStartPoint_;
  TouchPoint lastTouchPoint_;
};
