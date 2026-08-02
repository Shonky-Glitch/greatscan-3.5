#include "display_ui.h"

#include <Arduino.h>
#include <cstring>
#include <math.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#if __has_include(<esp32-hal-periman.h>)
#define GS_GFX_AVAILABLE 1
#include <Arduino_GFX_Library.h>
#else
#define GS_GFX_AVAILABLE 0
#endif

#include "project_config.h"

namespace {
// Flat automotive-diagnostic theme constants. Status/chrome colors are
// defined once here rather than scattered as literals through the drawing
// code, so the whole UI shares one consistent palette (same convention used
// by the CAN Sniffer 2000 display UI).
constexpr uint16_t kColorHeader = 0x39E7;
constexpr uint16_t kColorSeparator = TFT_DARKGREY;
constexpr uint16_t kColorOk = TFT_GREEN;
constexpr uint16_t kColorCaution = TFT_YELLOW;
constexpr uint16_t kColorWarn = TFT_ORANGE;
constexpr uint16_t kColorFault = TFT_RED;
constexpr uint16_t kColorUnknown = TFT_DARKGREY;

TFT_eSPI tft;

#if GS_GFX_AVAILABLE
Arduino_DataBus* gfxBus = nullptr;
Arduino_GFX* gfx = nullptr;
#endif

enum class DisplayBackend : uint8_t {
  None = 0,
  TftEspi,
  ArduinoGfx,
};

DisplayBackend backend = DisplayBackend::None;
bool deferredI2cScanDone = false;
bool dashboardFrameDrawn = false;
bool touchDiagnosticsFrameDrawn = false;
int dashboardFrameWidth = 0;
int dashboardFrameHeight = 0;
uint8_t dashboardFramePageIndex = 255;
uint8_t currentPageIndicatorIndex = 0;
float renderedLeftValue = -1000.0f;
float renderedRightValue = -1000.0f;
  float currentRpm = 0.0f;
  float currentCoolantC = 0.0f;
  float currentSpeedKph = 0.0f;
  float currentBoostKpa = 0.0f;
  float currentFuelPct = 0.0f;
  float currentThrottlePct = 0.0f;
  float currentDpfPct = 0.0f;
  float currentOilPressureKpa = 0.0f;
  float currentAfr = 14.7f;
  float currentOilTempC = 0.0f;
  float currentVoltage = 0.0f;
  float currentAtfC = 0.0f;
  float currentEg3C = 0.0f;
  float currentO2s1V = 0.0f;
  float currentEngineSpeedDidRpm = 0.0f;
DisplayUi::LinkState currentLinkState = DisplayUi::LinkState::Searching;
uint8_t lastRenderedLinkState = 255;
char detectedVehicleName[12] = "AUTO";
char lastRenderedVehicleName[12] = "";
bool lastRenderedTouchReady = false;
unsigned long lastTouchDiagDrawMs = 0;

constexpr float kMaxRpm = 5000.0f;
constexpr float kMinCoolantC = 40.0f;
constexpr float kMaxCoolantC = 120.0f;
constexpr float kMaxOilPressureKpa = 800.0f;
constexpr float kKpaToPsi = 0.14503774f;
constexpr float kMaxAfr = 25.0f;
constexpr float kMaxOilTempC = 160.0f;
constexpr float kMaxVoltage = 16.0f;
constexpr float kMaxAtfC = 180.0f;
constexpr float kMaxEg3C = 900.0f;
constexpr float kMaxO2s1V = 1.275f;
constexpr uint8_t kFt6336TouchCountReg = 0x02;
constexpr uint8_t kFt6336P1XhReg = 0x03;
constexpr uint8_t kUiPageCount = 6;
constexpr int kSwipeMinDistancePx = 70;
constexpr int kSwipeAxisBiasPx = 20;
constexpr unsigned long kTouchActionDebounceMs = 220;
constexpr unsigned long kCustomizeHoldMs = 5000;

struct UiButton {
  int x;
  int y;
  int w;
  int h;
};

struct PageConfig {
  const char* title;
  const char* leftLabel;
  const char* rightLabel;
  uint8_t leftMetric;
  uint8_t rightMetric;
  float leftValue;
  float rightValue;
  float leftMax;
  float rightMax;
  uint16_t leftColor;
  uint16_t rightColor;
};

enum class MetricId : uint8_t {
  Rpm = 0,
  Coolant,
  Speed,
  Boost,
  Fuel,
  Throttle,
  Dpf,
  OilPressure,
  Afr,
  OilTemp,
  Voltage,
  Atf,
  Eg3,
  O2s1,
  RpmDid,
  Count
};

struct MetricDef {
  const char* shortLabel;
  float maxValue;
  uint16_t color;
};

constexpr MetricDef kMetricDefs[] = {
    {"RPM", kMaxRpm, TFT_CYAN},
    {"COOL", kMaxCoolantC, TFT_ORANGE},
    {"SPEED", 200.0f, TFT_GREEN},
  {"BOOST", 200.0f * kKpaToPsi, TFT_MAGENTA},
  {"FUEL", ProjectConfig::kFuelTankLiters, TFT_CYAN},
    {"THRTL", 100.0f, TFT_ORANGE},
    {"DPF", 100.0f, TFT_YELLOW},
    {"OILP", kMaxOilPressureKpa, TFT_ORANGE},
    {"AFR", kMaxAfr, TFT_CYAN},
    {"OILT", kMaxOilTempC, TFT_YELLOW},
    {"VOLT", kMaxVoltage, TFT_GREEN},
    {"ATF", kMaxAtfC, TFT_ORANGE},
    {"EG3", kMaxEg3C, TFT_RED},
    {"O2S1", kMaxO2s1V, TFT_CYAN},
    {"RPMD", kMaxRpm, TFT_GREEN},
};

static_assert((sizeof(kMetricDefs) / sizeof(kMetricDefs[0])) == static_cast<size_t>(MetricId::Count), "Metric table mismatch");

UiButton diagButton = {0, 0, 0, 0};
UiButton leftCardButton = {0, 0, 0, 0};
UiButton rightCardButton = {0, 0, 0, 0};
UiButton readDtcButton = {0, 0, 0, 0};
UiButton clearDtcButton = {0, 0, 0, 0};
UiButton moduleScanButton = {0, 0, 0, 0};
char diagActionStatus[64] = "idle";
char lastRenderedDiagActionStatus[64] = "";

MetricId pageLeftMetric[kUiPageCount] = {
    MetricId::Rpm,
    MetricId::Speed,
    MetricId::Fuel,
    MetricId::OilPressure,
    MetricId::OilTemp,
  MetricId::Atf,
};

MetricId pageRightMetric[kUiPageCount] = {
    MetricId::Coolant,
    MetricId::Boost,
    MetricId::Dpf,
    MetricId::Afr,
    MetricId::Voltage,
  MetricId::Eg3,
};

float valueForMetric(MetricId metric) {
  switch (metric) {
    case MetricId::Rpm:
      return currentRpm;
    case MetricId::Coolant:
      return currentCoolantC;
    case MetricId::Speed:
      return currentSpeedKph;
    case MetricId::Boost:
      return currentBoostKpa * kKpaToPsi;
    case MetricId::Fuel:
      return (currentFuelPct * ProjectConfig::kFuelTankLiters) / 100.0f;
    case MetricId::Throttle:
      return currentThrottlePct;
    case MetricId::Dpf:
      return currentDpfPct;
    case MetricId::OilPressure:
      return currentOilPressureKpa;
    case MetricId::Afr:
      return currentAfr;
    case MetricId::OilTemp:
      return currentOilTempC;
    case MetricId::Voltage:
      return currentVoltage;
    case MetricId::Atf:
      return currentAtfC;
    case MetricId::Eg3:
      return currentEg3C;
    case MetricId::O2s1:
      return currentO2s1V;
    case MetricId::RpmDid:
      return currentEngineSpeedDidRpm;
    case MetricId::Count:
    default:
      return 0.0f;
  }
}

MetricId nextMetric(MetricId metric) {
  const uint8_t next = (static_cast<uint8_t>(metric) + 1U) % static_cast<uint8_t>(MetricId::Count);
  return static_cast<MetricId>(next);
}

void formatMetricValue(MetricId metric, float value, char* bigText, size_t bigSize, char* smallText, size_t smallSize) {
  switch (metric) {
    case MetricId::Rpm:
      snprintf(bigText, bigSize, "%4.0f", value);
      snprintf(smallText, smallSize, "%4.0f / %.0f", value, kMetricDefs[static_cast<uint8_t>(metric)].maxValue);
      return;
    case MetricId::Coolant:
    case MetricId::OilTemp:
    case MetricId::Atf:
    case MetricId::Eg3:
      snprintf(bigText, bigSize, "%5.1f C", value);
      snprintf(smallText, smallSize, "%5.1f C", value);
      return;
    case MetricId::Speed:
      snprintf(bigText, bigSize, "%4.0f", value);
      snprintf(smallText, smallSize, "%4.0f km/h", value);
      return;
    case MetricId::Boost:
      snprintf(bigText, bigSize, "%4.1f", value);
      snprintf(smallText, smallSize, "%5.1f psi", value);
      return;
    case MetricId::OilPressure:
      snprintf(bigText, bigSize, "%5.0f", value);
      snprintf(smallText, smallSize, "%5.0f kPa", value);
      return;
    case MetricId::O2s1:
      snprintf(bigText, bigSize, "%4.3f V", value);
      snprintf(smallText, smallSize, "%4.3f V", value);
      return;
    case MetricId::RpmDid:
      snprintf(bigText, bigSize, "%4.0f", value);
      snprintf(smallText, smallSize, "%4.0f RPM", value);
      return;
    case MetricId::Fuel:
      snprintf(bigText, bigSize, "%5.1fL", value);
      snprintf(smallText, smallSize, "%5.1f L left", value);
      return;
    case MetricId::Throttle:
      snprintf(bigText, bigSize, "%5.1f%%", value);
      snprintf(smallText, smallSize, "%5.1f %%", value);
      return;
    case MetricId::Dpf:
      if (value > 0.0f) {
        snprintf(bigText, bigSize, "%5.1f%%", value);
        snprintf(smallText, smallSize, "%5.1f %%", value);
      } else {
        snprintf(bigText, bigSize, "N/A");
        snprintf(smallText, smallSize, "no data");
      }
      return;
    case MetricId::Afr:
      snprintf(bigText, bigSize, "%4.2f", value);
      snprintf(smallText, smallSize, "stoich 14.70");
      return;
    case MetricId::Voltage:
      snprintf(bigText, bigSize, "%4.2f V", value);
      snprintf(smallText, smallSize, "battery");
      return;
    case MetricId::Count:
    default:
      snprintf(bigText, bigSize, "-");
      snprintf(smallText, smallSize, "-");
      return;
  }
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

PageConfig activePageConfig() {
  const uint8_t safePage = currentPageIndicatorIndex % kUiPageCount;
  const MetricId leftMetric = pageLeftMetric[safePage];
  const MetricId rightMetric = pageRightMetric[safePage];
  const MetricDef& leftDef = kMetricDefs[static_cast<uint8_t>(leftMetric)];
  const MetricDef& rightDef = kMetricDefs[static_cast<uint8_t>(rightMetric)];

  static char title[32];
  snprintf(title, sizeof(title), "P%u %s/%s", safePage + 1U, leftDef.shortLabel, rightDef.shortLabel);

  return {
      title,
      leftDef.shortLabel,
      rightDef.shortLabel,
      static_cast<uint8_t>(leftMetric),
      static_cast<uint8_t>(rightMetric),
      valueForMetric(leftMetric),
      valueForMetric(rightMetric),
      leftDef.maxValue,
      rightDef.maxValue,
      leftDef.color,
      rightDef.color,
  };
}

bool writeExpanderRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ProjectConfig::kLcdResetExpanderAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readExpanderRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(ProjectConfig::kLcdResetExpanderAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(ProjectConfig::kLcdResetExpanderAddress, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool writeTouchRegisterAddress(uint8_t reg) {
  Wire.beginTransmission(ProjectConfig::kTouchI2cAddress);
  Wire.write(reg);
  return Wire.endTransmission(false) == 0;
}

bool readTouchRegisters(uint8_t startReg, uint8_t* buffer, size_t length) {
  if (buffer == nullptr || length == 0) {
    return false;
  }
  if (!writeTouchRegisterAddress(startReg)) {
    return false;
  }
  const uint8_t readCount = Wire.requestFrom(static_cast<uint8_t>(ProjectConfig::kTouchI2cAddress), static_cast<uint8_t>(length));
  if (readCount != length) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    buffer[i] = Wire.read();
  }
  return true;
}

bool pulseLcdResetExpander() {
  uint8_t config = 0xFF;
  uint8_t output = 0x00;
  if (!readExpanderRegister(0x03, config)) {
    return false;
  }
  if (!readExpanderRegister(0x01, output)) {
    return false;
  }

  const uint8_t pinMask = ProjectConfig::kLcdResetExpanderPinMask;
  config &= static_cast<uint8_t>(~pinMask);
  output |= pinMask;
  if (!writeExpanderRegister(0x03, config)) {
    return false;
  }
  if (!writeExpanderRegister(0x01, output)) {
    return false;
  }
  delay(10);
  output &= static_cast<uint8_t>(~pinMask);
  if (!writeExpanderRegister(0x01, output)) {
    return false;
  }
  delay(10);
  output |= pinMask;
  if (!writeExpanderRegister(0x01, output)) {
    return false;
  }
  delay(200);
  return true;
}

void logI2cScan() {
  int found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      ++found;
      Serial.printf("[UI] i2c found 0x%02X\n", address);
    }
  }
  Serial.printf("[UI] i2c scan complete found=%d\n", found);
}

int panelWidth() {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      return gfx->width();
    }
#endif
  }
  return tft.width();
}

int panelHeight() {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      return gfx->height();
    }
#endif
  }
  return tft.height();
}

void fillScreen(uint16_t color) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->fillScreen(color);
      return;
    }
#endif
  }
  tft.fillScreen(color);
}

void fillRect(int x, int y, int w, int h, uint16_t color) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->fillRect(x, y, w, h, color);
      return;
    }
#endif
  }
  tft.fillRect(x, y, w, h, color);
}

void drawRect(int x, int y, int w, int h, uint16_t color) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->drawRect(x, y, w, h, color);
      return;
    }
#endif
  }
  tft.drawRect(x, y, w, h, color);
}

void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->drawLine(x0, y0, x1, y1, color);
      return;
    }
#endif
  }
  tft.drawLine(x0, y0, x1, y1, color);
}

void setTextColor(uint16_t fg, uint16_t bg) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->setTextColor(fg, bg);
      return;
    }
#endif
  }
  tft.setTextColor(fg, bg);
}

void setTextSize(uint8_t size) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->setTextSize(size);
      return;
    }
#endif
  }
  tft.setTextSize(size);
}

void setCursor(int x, int y) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->setCursor(x, y);
      return;
    }
#endif
  }
  tft.setCursor(x, y);
}

void printText(const char* text) {
  if (backend == DisplayBackend::ArduinoGfx) {
#if GS_GFX_AVAILABLE
    if (gfx != nullptr) {
      gfx->print(text);
      return;
    }
#endif
  }
  tft.print(text);
}

void drawTextAt(int x, int y, const char* text, uint16_t fg, uint16_t bg, uint8_t size) {
  setTextColor(fg, bg);
  setTextSize(size);
  setCursor(x, y);
  printText(text);
}

constexpr unsigned long kSplashDurationMs = 5000;

void drawSplashScreen() {
  const int w = panelWidth();
  const int h = panelHeight();
  const char* text = "GREATSCAN 3.5";
  const uint8_t textSize = 4;
  const int charW = 6 * textSize;
  const int charH = 8 * textSize;
  const int textW = static_cast<int>(strlen(text)) * charW;

  fillScreen(TFT_BLACK);
  drawRect(0, 0, w, h, TFT_WHITE);
  drawTextAt((w - textW) / 2, (h - charH) / 2, text, TFT_CYAN, TFT_BLACK, textSize);
}

uint8_t currentLinkStateCode() {
  return static_cast<uint8_t>(currentLinkState);
}

void drawLinkBadge(bool force) {
  const int w = panelWidth();
  const int badgeX = w - 118;
  const int badgeY = 6;
  const int badgeW = 110;
  const int badgeH = 24;

  const uint8_t state = currentLinkStateCode();
  if (!force && state == lastRenderedLinkState) {
    return;
  }

  uint16_t bg = kColorFault;
  uint16_t fg = TFT_WHITE;
  const char* label = "FAULT";
  switch (static_cast<DisplayUi::LinkState>(state)) {
    case DisplayUi::LinkState::Searching:
      bg = kColorUnknown;
      fg = TFT_WHITE;
      label = "SEARCH";
      break;
    case DisplayUi::LinkState::Rejoining:
      bg = kColorCaution;
      fg = TFT_BLACK;
      label = "REJOIN";
      break;
    case DisplayUi::LinkState::Stale:
      bg = kColorWarn;
      fg = TFT_BLACK;
      label = "STALE";
      break;
    case DisplayUi::LinkState::Connected:
      bg = kColorOk;
      fg = TFT_BLACK;
      label = "LIVE";
      break;
    case DisplayUi::LinkState::Fault:
    default:
      bg = kColorFault;
      fg = TFT_WHITE;
      label = "FAULT";
      break;
  }

  fillRect(badgeX, badgeY, badgeW, badgeH, bg);
  drawRect(badgeX, badgeY, badgeW, badgeH, TFT_WHITE);
  drawTextAt(badgeX + 12, badgeY + 6, label, fg, bg, 2);
  lastRenderedLinkState = state;
}

void drawPageIndicator(uint8_t pageIndex, uint16_t headerColor) {
  const int w = panelWidth();
  const int chipX = w - 168;
  const int chipY = 8;
  const int chipW = 42;
  const int chipH = 20;
  char pageText[8];
  snprintf(pageText, sizeof(pageText), "%u/%u", pageIndex + 1, kUiPageCount);
  fillRect(chipX, chipY, chipW, chipH, headerColor);
  drawRect(chipX, chipY, chipW, chipH, TFT_WHITE);
  drawTextAt(chipX + 10, chipY + 6, pageText, TFT_WHITE, headerColor, 1);
}

void drawVehicleBadge(bool force) {
  if (!force && std::strncmp(detectedVehicleName, lastRenderedVehicleName, sizeof(lastRenderedVehicleName)) == 0) {
    return;
  }

  const int badgeX = 4;
  const int badgeY = 6;
  const int badgeW = 80;
  const int badgeH = 24;
  uint16_t bg = TFT_DARKGREY;
  uint16_t fg = TFT_WHITE;

  if (std::strcmp(detectedVehicleName, "FORD") == 0) {
    bg = TFT_BLUE;
    fg = TFT_WHITE;
  } else if (std::strcmp(detectedVehicleName, "TOYOTA") == 0) {
    bg = TFT_RED;
    fg = TFT_WHITE;
  }

  fillRect(badgeX, badgeY, badgeW, badgeH, bg);
  drawRect(badgeX, badgeY, badgeW, badgeH, TFT_WHITE);
  drawTextAt(badgeX + 8, badgeY + 6, detectedVehicleName, fg, bg, 1);
  std::snprintf(lastRenderedVehicleName, sizeof(lastRenderedVehicleName), "%s", detectedVehicleName);
}

void drawDashboardFrame() {
  const int w = panelWidth();
  const int h = panelHeight();
  const PageConfig page = activePageConfig();
  const bool needsClear = !dashboardFrameDrawn || dashboardFrameWidth != w || dashboardFrameHeight != h;
  const bool pageChanged = dashboardFramePageIndex != currentPageIndicatorIndex;

  dashboardFrameWidth = w;
  dashboardFrameHeight = h;
  dashboardFrameDrawn = true;
  dashboardFramePageIndex = currentPageIndicatorIndex;
  renderedLeftValue = -1000.0f;
  renderedRightValue = -1000.0f;

  if (needsClear) {
    fillScreen(TFT_BLACK);
  }
  if (pageChanged) {
    const int top = 36;
    const int bottom = 36;
    fillRect(0, top, w, h - top - bottom, TFT_BLACK);
  }
  drawRect(0, 0, w, h, TFT_WHITE);

  fillRect(0, 0, w, 36, kColorHeader);
  drawVehicleBadge(true);
  drawTextAt(88, 10, "GreatScan 3.5", TFT_WHITE, kColorHeader, 2);
  drawPageIndicator(currentPageIndicatorIndex, kColorHeader);
  lastRenderedLinkState = 255;
  drawLinkBadge(true);

  drawRect(0, 36, w, 1, kColorSeparator);

  const int gap = 12;
  const int top = 52;
  const int buttonH = 32;
  const int buttonY = h - 12 - buttonH;
  const int hintY = buttonY - 14;
  const int cardH = hintY - 8 - top;
  const int cardW = (w - (gap * 3)) / 2;
  const int leftX = gap;
  const int rightX = leftX + cardW + gap;

  leftCardButton = {leftX, top, cardW, cardH};
  rightCardButton = {rightX, top, cardW, cardH};

  drawRect(leftX, top, cardW, cardH, page.leftColor);
  drawRect(rightX, top, cardW, cardH, page.rightColor);

  drawTextAt(leftX + 12, top + 16, page.leftLabel, page.leftColor, TFT_BLACK, 3);
  drawTextAt(rightX + 12, top + 16, page.rightLabel, page.rightColor, TFT_BLACK, 3);
  drawTextAt(12, hintY, "Hold gauge cards 5s to customize", TFT_DARKGREY, TFT_BLACK, 1);

  fillRect(leftX + 12, top + 80, cardW - 24, 54, TFT_BLACK);
  fillRect(rightX + 12, top + 80, cardW - 24, 54, TFT_BLACK);
  fillRect(leftX + 12, top + 160, cardW - 24, 26, TFT_BLACK);
  fillRect(rightX + 12, top + 160, cardW - 24, 26, TFT_BLACK);

  const int buttonW = 160;
  diagButton = {(w - buttonW) / 2, buttonY, buttonW, buttonH};

  fillRect(diagButton.x, diagButton.y, diagButton.w, diagButton.h, TFT_DARKGREY);
  drawRect(diagButton.x, diagButton.y, diagButton.w, diagButton.h, TFT_WHITE);
  drawTextAt(diagButton.x + 44, diagButton.y + 8, "DIAG", TFT_WHITE, TFT_DARKGREY, 2);
}

void renderDashboardValues(bool force) {
  const int w = panelWidth();
  const int h = panelHeight();
  if (!dashboardFrameDrawn || dashboardFrameWidth != w || dashboardFrameHeight != h) {
    drawDashboardFrame();
  }

  const PageConfig page = activePageConfig();

  const int gap = 12;
  const int top = 52;
  const int cardW = (w - (gap * 3)) / 2;
  const int leftX = gap;
  const int rightX = leftX + cardW + gap;
  const int innerW = cardW - 24;

  const float leftValue = clampFloat(page.leftValue, 0.0f, page.leftMax);
  const float rightValue = clampFloat(page.rightValue, 0.0f, page.rightMax);

  const bool leftChanged = force || fabsf(leftValue - renderedLeftValue) >= 0.2f;
  const bool rightChanged = force || fabsf(rightValue - renderedRightValue) >= 0.2f;

  drawLinkBadge(force);
  drawVehicleBadge(force);

  if (leftChanged) {
    fillRect(leftX + 12, top + 80, innerW, 54, TFT_BLACK);
    fillRect(leftX + 12, top + 160, innerW, 26, TFT_BLACK);
    char leftText[20];
    char leftLabel[24];
    formatMetricValue(static_cast<MetricId>(page.leftMetric), leftValue, leftText, sizeof(leftText), leftLabel, sizeof(leftLabel));
    drawTextAt(leftX + 12, top + 90, leftText, TFT_WHITE, TFT_BLACK, 5);
    drawTextAt(leftX + 12, top + 168, leftLabel, page.leftColor, TFT_BLACK, 2);
    renderedLeftValue = leftValue;
  }

  if (rightChanged) {
    fillRect(rightX + 12, top + 80, innerW, 54, TFT_BLACK);
    fillRect(rightX + 12, top + 160, innerW, 26, TFT_BLACK);
    char rightText[20];
    char rightLabel[24];
    formatMetricValue(static_cast<MetricId>(page.rightMetric), rightValue, rightText, sizeof(rightText), rightLabel, sizeof(rightLabel));
    drawTextAt(rightX + 12, top + 90, rightText, TFT_WHITE, TFT_BLACK, 5);
    drawTextAt(rightX + 12, top + 168, rightLabel, page.rightColor, TFT_BLACK, 2);
    renderedRightValue = rightValue;
  }
}

void drawTouchDiagnosticsFrame() {
  const int w = panelWidth();
  const int h = panelHeight();
  const bool needsClear = !touchDiagnosticsFrameDrawn;
  touchDiagnosticsFrameDrawn = true;

  if (needsClear) {
    fillScreen(TFT_BLACK);
  }
  drawRect(0, 0, w, h, TFT_WHITE);
  fillRect(0, 0, w, 36, TFT_DARKGREY);
  drawTextAt(12, 10, "DIAGNOSTICS", TFT_WHITE, TFT_DARKGREY, 2);
  drawRect(0, 36, w, 1, kColorSeparator);
  drawTextAt(12, 48, "Tap DIAG button to return dashboard", TFT_CYAN, TFT_BLACK, 1);

  const int actionY = 84;
  const int actionH = 140;
  const int actionMargin = 10;
  const int actionGap = 8;
  const int actionW = (w - (actionMargin * 2) - (actionGap * 2)) / 3;
  readDtcButton = {actionMargin, actionY, actionW, actionH};
  clearDtcButton = {actionMargin + actionW + actionGap, actionY, actionW, actionH};
  moduleScanButton = {actionMargin + (actionW + actionGap) * 2, actionY, actionW, actionH};

  fillRect(readDtcButton.x, readDtcButton.y, readDtcButton.w, readDtcButton.h, TFT_NAVY);
  drawRect(readDtcButton.x, readDtcButton.y, readDtcButton.w, readDtcButton.h, TFT_WHITE);
  drawTextAt(readDtcButton.x + 20, readDtcButton.y + 52, "READ", TFT_WHITE, TFT_NAVY, 3);

  fillRect(clearDtcButton.x, clearDtcButton.y, clearDtcButton.w, clearDtcButton.h, TFT_MAROON);
  drawRect(clearDtcButton.x, clearDtcButton.y, clearDtcButton.w, clearDtcButton.h, TFT_WHITE);
  drawTextAt(clearDtcButton.x + 10, clearDtcButton.y + 52, "CLEAR", TFT_WHITE, TFT_MAROON, 3);

  fillRect(moduleScanButton.x, moduleScanButton.y, moduleScanButton.w, moduleScanButton.h, TFT_GREEN);
  drawRect(moduleScanButton.x, moduleScanButton.y, moduleScanButton.w, moduleScanButton.h, TFT_WHITE);
  drawTextAt(moduleScanButton.x + 16, moduleScanButton.y + 52, "SCAN", TFT_BLACK, TFT_GREEN, 3);

  fillRect(12, 232, w - 24, 26, TFT_BLACK);
  drawTextAt(12, 236, "Diag: idle", TFT_YELLOW, TFT_BLACK, 2);

  const int buttonW = 160;
  const int buttonH = 32;
  const int buttonY = h - 12 - buttonH;
  diagButton = {(w - buttonW) / 2, buttonY, buttonW, buttonH};

  fillRect(diagButton.x, diagButton.y, diagButton.w, diagButton.h, TFT_DARKGREY);
  drawRect(diagButton.x, diagButton.y, diagButton.w, diagButton.h, TFT_WHITE);
  drawTextAt(diagButton.x + 44, diagButton.y + 8, "DIAG", TFT_WHITE, TFT_DARKGREY, 2);
}

}  // namespace

bool DisplayUi::readTouchPoint(TouchPoint& point) {
  uint8_t touchCount = 0;
  if (!readTouchRegisters(kFt6336TouchCountReg, &touchCount, 1)) {
    return false;
  }
  touchCount &= 0x0F;
  if (touchCount == 0) {
    return false;
  }

  uint8_t raw[4] = {};
  if (!readTouchRegisters(kFt6336P1XhReg, raw, sizeof(raw))) {
    return false;
  }

  int rawX = static_cast<int>(((raw[0] & 0x0F) << 8) | raw[1]);
  int rawY = static_cast<int>(((raw[2] & 0x0F) << 8) | raw[3]);
  rawX = constrain(rawX, 0, ProjectConfig::kTouchRawMaxX - 1);
  rawY = constrain(rawY, 0, ProjectConfig::kTouchRawMaxY - 1);

  int mappedX = rawX;
  int mappedY = rawY;
  if (ProjectConfig::kTouchSwapXY) {
    mappedX = rawY;
    mappedY = rawX;
  }

  const int w = panelWidth();
  const int h = panelHeight();
  mappedX = map(mappedX, 0, ProjectConfig::kTouchSwapXY ? (ProjectConfig::kTouchRawMaxY - 1) : (ProjectConfig::kTouchRawMaxX - 1), 0, w - 1);
  mappedY = map(mappedY, 0, ProjectConfig::kTouchSwapXY ? (ProjectConfig::kTouchRawMaxX - 1) : (ProjectConfig::kTouchRawMaxY - 1), 0, h - 1);

  if (ProjectConfig::kTouchInvertX) {
    mappedX = (w - 1) - mappedX;
  }
  if (ProjectConfig::kTouchInvertY) {
    mappedY = (h - 1) - mappedY;
  }

  point.x = constrain(mappedX, 0, w - 1);
  point.y = constrain(mappedY, 0, h - 1);
  return true;
}

void DisplayUi::handleTouchTap(const TouchPoint& point) {
  const unsigned long now = millis();
  if (now - lastTouchActionMs_ < kTouchActionDebounceMs) {
    return;
  }

  lastTouchPoint_ = point;
  Serial.printf("[UI] touch tap x=%d y=%d\n", point.x, point.y);

  const auto inside = [&](const UiButton& button) {
    return point.x >= button.x && point.x < (button.x + button.w) && point.y >= button.y && point.y < (button.y + button.h);
  };

  if (showTouchDiagnostics_) {
    if (inside(readDtcButton)) {
      lastTouchActionMs_ = now;
      pendingAction_ = Action::ReadDtc;
      std::snprintf(diagActionStatus, sizeof(diagActionStatus), "queued read dtc");
      lastRenderedDiagActionStatus[0] = '\0';
      Serial.println("[UI] action read-dtc");
      return;
    }

    if (inside(clearDtcButton)) {
      lastTouchActionMs_ = now;
      pendingAction_ = Action::ClearDtc;
      std::snprintf(diagActionStatus, sizeof(diagActionStatus), "queued clear dtc");
      lastRenderedDiagActionStatus[0] = '\0';
      Serial.println("[UI] action clear-dtc");
      return;
    }

    if (inside(moduleScanButton)) {
      lastTouchActionMs_ = now;
      pendingAction_ = Action::ModuleScan;
      std::snprintf(diagActionStatus, sizeof(diagActionStatus), "queued module scan");
      lastRenderedDiagActionStatus[0] = '\0';
      Serial.println("[UI] action module-scan");
      return;
    }
  }

  if (inside(diagButton)) {
    lastTouchActionMs_ = now;
    showTouchDiagnostics_ = !showTouchDiagnostics_;
    if (showTouchDiagnostics_) {
      drawTouchDiagnostics(true);
      Serial.println("[UI] touch diagnostics page enabled");
    } else {
      showActivePage();
    }
    return;
  }

  if (point.y < 36) {
    lastTouchActionMs_ = now;
    showTouchDiagnostics_ = !showTouchDiagnostics_;
    if (showTouchDiagnostics_) {
      drawTouchDiagnostics(true);
      Serial.println("[UI] touch diagnostics page enabled");
    } else {
      showActivePage();
    }
  }
}

void DisplayUi::handleTouchRelease(const TouchPoint& point) {
  const unsigned long now = millis();
  if (now - lastTouchActionMs_ < kTouchActionDebounceMs) {
    return;
  }

  const int dx = point.x - touchStartPoint_.x;
  const int dy = point.y - touchStartPoint_.y;
  const int absDx = abs(dx);
  const int absDy = abs(dy);

  if (absDx >= kSwipeMinDistancePx && absDx > (absDy + kSwipeAxisBiasPx)) {
    lastTouchActionMs_ = now;
    if (dx < 0) {
      cyclePage(1);
      Serial.println("[UI] swipe left -> next page");
    } else {
      cyclePage(-1);
      Serial.println("[UI] swipe right -> previous page");
    }
    return;
  }

  handleTouchTap(point);
}

void DisplayUi::cyclePage(int step) {
  int index = static_cast<int>(pageIndex_) + step;
  while (index < 0) {
    index += kUiPageCount;
  }
  while (index >= kUiPageCount) {
    index -= kUiPageCount;
  }
  pageIndex_ = static_cast<uint8_t>(index);
  if (!showTouchDiagnostics_) {
    showActivePage();
  }
  Serial.printf("[UI] page switched: %u\n", pageIndex_ + 1);
}

void DisplayUi::showActivePage() {
  currentPageIndicatorIndex = pageIndex_;
  // Force a clean frame rebuild when switching page to prevent mixed underlays.
  dashboardFrameDrawn = false;
  touchDiagnosticsFrameDrawn = false;
  drawDashboardFrame();
  renderDashboardValues(true);
  Serial.printf("[UI] page enabled: %u\n", pageIndex_ + 1);
}

void DisplayUi::pollTouch() {
  if (!touchControllerReady_) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastTouchPollMs_ < ProjectConfig::kTouchPollMs) {
    return;
  }
  lastTouchPollMs_ = now;

  TouchPoint point;
  const bool isDown = readTouchPoint(point);

  if (isDown && !touchWasDown_) {
    touchStartPoint_ = point;
    touchDownStartMs_ = now;
    touchHoldCustomizeTriggered_ = false;
    touchTracking_ = true;
  }

  if (isDown) {
    lastTouchPoint_ = point;

    if (!showTouchDiagnostics_ && !touchHoldCustomizeTriggered_ && (now - touchDownStartMs_ >= kCustomizeHoldMs)) {
      const auto insideAt = [&](const UiButton& button, const TouchPoint& p) {
        return p.x >= button.x && p.x < (button.x + button.w) && p.y >= button.y && p.y < (button.y + button.h);
      };

      const uint8_t page = pageIndex_ % kUiPageCount;
      if (insideAt(leftCardButton, touchStartPoint_)) {
        touchHoldCustomizeTriggered_ = true;
        lastTouchActionMs_ = now;
        pageLeftMetric[page] = nextMetric(pageLeftMetric[page]);
        showActivePage();
        Serial.printf("[UI] page %u left metric -> %s (hold 5s)\n", page + 1, kMetricDefs[static_cast<uint8_t>(pageLeftMetric[page])].shortLabel);
        return;
      }

      if (insideAt(rightCardButton, touchStartPoint_)) {
        touchHoldCustomizeTriggered_ = true;
        lastTouchActionMs_ = now;
        pageRightMetric[page] = nextMetric(pageRightMetric[page]);
        showActivePage();
        Serial.printf("[UI] page %u right metric -> %s (hold 5s)\n", page + 1, kMetricDefs[static_cast<uint8_t>(pageRightMetric[page])].shortLabel);
        return;
      }
    }
  }

  if (!isDown && touchWasDown_ && touchTracking_) {
    touchTracking_ = false;
    handleTouchRelease(lastTouchPoint_);
  }

  touchWasDown_ = isDown;
}

void DisplayUi::drawTouchDiagnostics(bool force) {
  if (!showTouchDiagnostics_) {
    return;
  }

  const unsigned long now = millis();
  const bool readyChanged = touchControllerReady_ != lastRenderedTouchReady;

  if (!force && !readyChanged && (now - lastTouchDiagDrawMs < 150)) {
    return;
  }

  if (force) {
    drawTouchDiagnosticsFrame();
  }

  if (force || readyChanged) {
    fillRect(12, 66, panelWidth() - 24, 18, TFT_BLACK);
    drawTextAt(12, 68, touchControllerReady_ ? "FT6336: ONLINE" : "FT6336: OFFLINE", touchControllerReady_ ? TFT_GREEN : TFT_RED, TFT_BLACK, 1);
    lastRenderedTouchReady = touchControllerReady_;
  }

  if (force || std::strncmp(diagActionStatus, lastRenderedDiagActionStatus, sizeof(lastRenderedDiagActionStatus)) != 0) {
    fillRect(12, 232, panelWidth() - 24, 26, TFT_BLACK);
    char statusLine[48];
    std::snprintf(statusLine, sizeof(statusLine), "Diag: %s", diagActionStatus);
    drawTextAt(12, 236, statusLine, TFT_YELLOW, TFT_BLACK, 2);
    std::snprintf(lastRenderedDiagActionStatus, sizeof(lastRenderedDiagActionStatus), "%s", diagActionStatus);
  }

  lastTouchDiagDrawMs = now;
}

void DisplayUi::begin() {
  currentPageIndicatorIndex = 0;
  Wire.begin(ProjectConfig::kTouchSdaPin, ProjectConfig::kTouchSclPin);
  logI2cScan();
  uint8_t touchCount = 0;
  touchControllerReady_ = readTouchRegisters(kFt6336TouchCountReg, &touchCount, 1);
  Serial.printf("[UI] FT6336 %s\n", touchControllerReady_ ? "detected" : "not detected");

  if (ProjectConfig::kLcdBacklightPin >= 0) {
    pinMode(ProjectConfig::kLcdBacklightPin, OUTPUT);
    digitalWrite(ProjectConfig::kLcdBacklightPin, LOW);
    delay(80);
  }

  if (pulseLcdResetExpander()) {
    Serial.println("[UI] LCD reset via TCA9554");
  } else {
    Serial.println("[UI] LCD reset expander not acknowledged");
  }

#if GS_GFX_AVAILABLE
  gfxBus = new Arduino_ESP32SPI(
      ProjectConfig::kLcdDcPin,
      ProjectConfig::kLcdCsPin,
      ProjectConfig::kLcdSclkPin,
      ProjectConfig::kLcdMosiPin,
      ProjectConfig::kLcdMisoPin);
  gfx = new Arduino_ST7796(
      gfxBus,
      ProjectConfig::kLcdRstPin,
      0,
      true,
      ProjectConfig::kLcdWidth,
      ProjectConfig::kLcdHeight,
      0,
      0,
      0,
      0);
  backend = DisplayBackend::ArduinoGfx;
  gfx->begin();
  gfx->setRotation(3);
  Serial.println("[UI] Backend: Arduino_GFX");
#else
  tft.init();
  tft.setRotation(3);
  backend = DisplayBackend::TftEspi;
  Serial.println("[UI] Backend: TFT_eSPI");
#endif

  if (ProjectConfig::kLcdBacklightPin >= 0) {
    digitalWrite(ProjectConfig::kLcdBacklightPin, HIGH);
    delay(80);
  }

  fillScreen(TFT_BLACK);
  delay(60);
  fillScreen(TFT_WHITE);
  delay(60);
  fillScreen(TFT_BLACK);

  Serial.println("[UI] FW marker: greatscan-3.5-gfx-dashboard");
  Serial.printf("[UI] panel=%dx%d\n", panelWidth(), panelHeight());

  Serial.println("[UI] Showing splash screen");
  drawSplashScreen();
  delay(kSplashDurationMs);

  drawDashboardFrame();
  renderDashboardValues(true);

  screenReady_ = true;
  Serial.println("[UI] Display initialized (GreatScan 3.5 dashboard)");
}

void DisplayUi::tick() {
  const unsigned long now = millis();
  if (!deferredI2cScanDone && now >= 10000) {
    deferredI2cScanDone = true;
    Serial.println("[UI] deferred i2c scan start");
    logI2cScan();
  }

  if (now - lastPrintMs_ >= 1000) {
    lastPrintMs_ = now;
    Serial.printf("[UI] rpm=%.0f coolant=%.1fC\n", currentRpm, currentCoolantC);
  }

  if (!screenReady_) {
    return;
  }

  pollTouch();

  if (showTouchDiagnostics_) {
    drawTouchDiagnostics(false);
  } else {
    renderDashboardValues(false);
  }
}

void DisplayUi::setGaugeValues(float rpm, float coolantC) {
  currentRpm = rpm;
  currentCoolantC = coolantC;
  lastRpm_ = rpm;
  lastCoolantC_ = coolantC;
}

void DisplayUi::setAuxValues(
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
  float engineSpeedDidRpm) {
  currentSpeedKph = speedKph;
  currentBoostKpa = boostKpa;
  currentFuelPct = fuelPct;
  currentThrottlePct = throttlePct;
  currentDpfPct = dpfPct;
  currentOilPressureKpa = oilPressureKpa;
  currentAfr = afr;
  currentOilTempC = oilTempC;
  currentVoltage = voltage;
  currentAtfC = atfC;
  currentEg3C = eg3C;
  currentO2s1V = o2s1V;
  currentEngineSpeedDidRpm = engineSpeedDidRpm;
}

void DisplayUi::setLinkState(LinkState state) {
  currentLinkState = state;
}

void DisplayUi::setVehicleName(const char* name) {
  const char* safe = (name && name[0] != '\0') ? name : "AUTO";
  if (std::strncmp(detectedVehicleName, safe, sizeof(detectedVehicleName)) == 0) {
    return;
  }

  std::snprintf(detectedVehicleName, sizeof(detectedVehicleName), "%s", safe);
  lastRenderedVehicleName[0] = '\0';
}

DisplayUi::Action DisplayUi::consumeAction() {
  const Action action = pendingAction_;
  pendingAction_ = Action::None;
  return action;
}

void DisplayUi::setDiagnosticResult(const char* status) {
  const char* safe = (status && status[0] != '\0') ? status : "idle";
  if (std::strncmp(diagActionStatus, safe, sizeof(diagActionStatus)) == 0) {
    return;
  }

  std::snprintf(diagActionStatus, sizeof(diagActionStatus), "%s", safe);
}
