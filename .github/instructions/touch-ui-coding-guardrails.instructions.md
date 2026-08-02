---
name: "Touch UI Coding Guardrails"
description: "Use when editing touchscreen UI, gauge rendering, or web gauge endpoints in the G-Scan ESP32-S3 interface project."
applyTo: src/ui/**, src/net/**, src/app/**
---
# Touch UI Coding Guardrails

- Keep loop work non-blocking. Do not add long delays in runtime paths executed from `App::tick()`.
- Preserve deterministic update cadence. Keep periodic work tied to millis-based intervals.
- Prefer explicit state flow through class members and setter methods instead of global mutable state.
- Minimize redraw flicker. Update only changed values where practical.
- Keep diagnostics stable with existing tags: `[BOOT]`, `[UI]`, `[WEB]`, `[GS]`, `[GS-RAW]`.
- Keep API compatibility for gauge payloads unless changes are requested.
: Existing keys are served in [src/net/web_ui.cpp](../../src/net/web_ui.cpp).
- Keep configuration values centralized in [include/project_config.h](../../include/project_config.h).
- Treat this project as UI/mirror transport for diagnostics. Avoid adding ECU write/flash logic here.
- For module scan or DTC UI additions, gracefully handle missing fields and stale upstream values.
- Web API integration checklist when changing payload handling:
: Validate parsing fallback defaults in [src/net/web_ui.cpp](../../src/net/web_ui.cpp).
: Keep existing key names stable unless upstream and UI are updated together.
: Preserve non-blocking poll cadence and timeout behavior.
: Add serial diagnostics with existing tags to trace parse errors.
- Validate compile before broader testing:
: `pio run --environment esp32-s3-ui`
: Then run targeted tests from [test/test_project_config/test_main.cpp](../../test/test_project_config/test_main.cpp) when config changes are involved.
- When touch behavior appears wrong, first verify pin and board assumptions in [platformio.ini](../../platformio.ini) and [include/project_config.h](../../include/project_config.h).
