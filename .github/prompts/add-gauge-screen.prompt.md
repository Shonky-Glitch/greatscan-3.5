---
name: "Add Gauge Screen"
description: "Add a new gauge screen with touch actions and matching web API updates for the G-Scan interface project."
argument-hint: "Describe the gauge type, units/range, touch actions, and whether web API fields should be added."
agent: "G-Scan Touch UI Gauges"
---
Create or update a gauge screen feature for this project.

Input requirements:
- Gauge name and metric source (for example RPM, boost, fuel rail pressure)
- Display format (units, min/max range, warning thresholds)
- Touch behavior (tap, hold, swipe, page navigation)
- Whether `/api/gauges` or `/api/greatscan` payload changes are required

Implementation rules:
- Keep changes localized to relevant modules:
: [src/ui/display_ui.h](../../src/ui/display_ui.h)
: [src/ui/display_ui.cpp](../../src/ui/display_ui.cpp)
: [src/app/app.cpp](../../src/app/app.cpp)
: [src/net/web_ui.cpp](../../src/net/web_ui.cpp)
- Preserve non-blocking loop behavior and existing log tags.
- If adding API fields, keep names concise and backward compatible when possible.

Validation:
1. Build with `pio run --environment esp32-s3-ui`.
2. If config constants change, run project config tests.
3. Summarize what changed, what was validated, and any remaining hardware assumptions.
