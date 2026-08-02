#include "telemetry_test_page.h"

#include <cstdio>
#include <cstring>

namespace greatscan {
namespace testpages {

namespace {

// 16-bit (565) color constants, kept local so this module has no dependency
// on TFT_eSPI's header -- values match the TFT_* constants used elsewhere
// in the UI (kColorHeader/TFT_GREEN/TFT_YELLOW/TFT_ORANGE/TFT_DARKGREY).
constexpr uint16_t kColorHeader = 0x39E7;
constexpr uint16_t kColorDarkGrey = 0x7BEF;
constexpr uint16_t kColorGreen = 0x07E0;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kColorOrange = 0xFD20;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorBlack = 0x0000;

// Source: Decoding 2000 (separate project) output/telemetry/candidates.csv,
// generated from real captures against this Ford PX2 Ranger's PCM
// (7E0 request / 7E8 response, UDS service 0x22 ReadDataByIdentifier). See
// that file's `notes` column for the full sourcing/reasoning behind each
// hypothesis. Only `051C` is field-confirmed; the rest are candidates that
// changed value across repeated reads and are worth a field test, not
// confirmed signal meanings.
constexpr TelemetryTestCode kTelemetryTestCodes[] = {
    {"7E8", "051C", "Air Charge Temp (ACT)", "confirmed"},
    {"7E8", "F433", "Temp sensor? (F4xx family)", "hypothesis-high"},
    {"7E8", "F43C", "Hi-res temp sensor? (F4xx)", "hypothesis-medium"},
    {"7E8", "F442", "Manifold/boost pressure?", "hypothesis-low"},
    {"7E8", "033C", "Secondary analog sensor?", "hypothesis-low"},
    {"7E8", "035A", "Analog sensor (mod. drift)?", "hypothesis-low"},
    {"7E8", "03BA", "Analog sensor (mod. drift)?", "hypothesis-low"},
    {"7E8", "0914", "Cyclic/stepped parameter?", "hypothesis-low"},
    {"7E8", "0915", "Paired parameter to 0914?", "hypothesis-low"},
    {"7E8", "9800", "Status/counter register?", "hypothesis-low"},
};

constexpr size_t kTelemetryTestCodeCount = sizeof(kTelemetryTestCodes) / sizeof(kTelemetryTestCodes[0]);

uint16_t colorForConfidence(const char* confidence) {
  if (std::strcmp(confidence, "confirmed") == 0) {
    return kColorGreen;
  }
  if (std::strcmp(confidence, "hypothesis-high") == 0) {
    return kColorYellow;
  }
  if (std::strcmp(confidence, "hypothesis-medium") == 0) {
    return kColorOrange;
  }
  return kColorDarkGrey;
}

bool framePainted = false;

}  // namespace

void drawTelemetryCodesPage(const DrawSurface& surface, bool force) {
  if (!force && framePainted) {
    return;
  }
  framePainted = true;

  if (surface.fillScreen == nullptr || surface.fillRect == nullptr ||
      surface.drawRect == nullptr || surface.drawTextAt == nullptr) {
    return;
  }

  const int w = surface.width;
  const int h = surface.height;

  surface.fillScreen(kColorBlack);
  surface.drawRect(0, 0, w, h, kColorWhite);
  surface.fillRect(0, 0, w, 36, kColorHeader);
  surface.drawTextAt(8, 10, "TELEMETRY TEST CODES", kColorWhite, kColorHeader, 2);
  surface.drawRect(0, 36, w, 1, kColorDarkGrey);
  surface.drawTextAt(8, 42, "Decoding 2000 candidates - Ford PX2 (7E0/7E8)", kColorDarkGrey, kColorBlack, 1);

  int y = 60;
  const int rowH = 24;
  for (size_t i = 0; i < kTelemetryTestCodeCount && (y + rowH) <= (h - 4); ++i) {
    const TelemetryTestCode& code = kTelemetryTestCodes[i];
    const uint16_t color = colorForConfidence(code.confidence);

    char line[40];
    std::snprintf(line, sizeof(line), "%s %s", code.did, code.name);
    surface.drawTextAt(8, y, line, color, kColorBlack, 1);

    surface.drawTextAt(w - 96, y, code.confidence, color, kColorBlack, 1);

    y += rowH;
  }
}

}  // namespace testpages
}  // namespace greatscan
