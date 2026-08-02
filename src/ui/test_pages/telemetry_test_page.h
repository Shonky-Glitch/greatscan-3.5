#pragma once

#include <stddef.h>
#include <stdint.h>

// Reference/test page listing candidate telemetry codes for this Ford PX2
// Ranger, as identified by the separate "Decoding 2000" CAN log analysis
// project (see ../../../.. /Decoding 2000/output/telemetry/candidates.csv).
// This is a bench/field-test aid for correlating raw ECU reads against the
// physical gauge cluster while wiring up new gauges -- it is intentionally
// kept in its own folder/module, separate from the live dashboard in
// display_ui.cpp, since it shows static reference data rather than live
// telemetry values.

namespace greatscan {
namespace testpages {

// Minimal drawing surface handed in by display_ui.cpp so this module stays
// backend-agnostic (TFT_eSPI vs Arduino_GFX) without needing direct access
// to the display driver internals.
struct DrawSurface {
  int width = 0;
  int height = 0;
  void (*fillScreen)(uint16_t color) = nullptr;
  void (*fillRect)(int x, int y, int w, int h, uint16_t color) = nullptr;
  void (*drawRect)(int x, int y, int w, int h, uint16_t color) = nullptr;
  void (*drawTextAt)(int x, int y, const char* text, uint16_t fg, uint16_t bg, uint8_t size) = nullptr;
};

// One candidate telemetry code, sourced from the Decoding 2000 telemetry
// candidate export for this vehicle. `confirmed` entries have a
// field-verified name; everything else is an unverified hypothesis awaiting
// a field test -- never treat a hypothesis entry as a confirmed fact.
struct TelemetryTestCode {
  const char* arbitrationId;  // Responding module CAN id (hex)
  const char* did;            // UDS (service 0x22) data identifier (hex)
  const char* name;           // Hypothesized or confirmed signal name
  const char* confidence;     // "confirmed" | "hypothesis-low/medium/high"
};

// Renders the "Telemetry Test Codes" reference page. Draws once per call
// with `force == true` (e.g. on page-enter); subsequent `force == false`
// calls from the render loop are a cheap no-op since the listed data never
// changes at runtime, keeping this safe to poll every tick.
void drawTelemetryCodesPage(const DrawSurface& surface, bool force);

}  // namespace testpages
}  // namespace greatscan
