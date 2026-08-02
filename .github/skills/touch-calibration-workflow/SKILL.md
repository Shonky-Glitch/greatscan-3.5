---
name: touch-calibration-workflow
description: 'Calibrate and verify touchscreen behavior on the G-Scan ESP32-S3 display stack. Use for touch mapping drift, wrong axis direction, missed taps, or coordinate scaling errors.'
argument-hint: 'Describe the touch issue, board variant, and current observed coordinates or gesture failures.'
user-invocable: true
disable-model-invocation: false
---
# Touch Calibration Workflow

Use this workflow when touchscreen input is offset, mirrored, rotated incorrectly, jittery, or intermittently missing.

## Scope
- ESP32-S3 G-Scan interface project touch behavior only.
- Calibration and verification for touch-to-UI mapping.

## Pre-Checks
1. Confirm board and environment settings in [platformio.ini](../../../platformio.ini).
2. Confirm touch-related pins and constants in [include/project_config.h](../../../include/project_config.h).
3. Confirm active UI draw orientation in [src/ui/display_ui.cpp](../../../src/ui/display_ui.cpp).

## Procedure
1. Baseline build:
: `pio run --environment esp32-s3-ui`
2. Add temporary serial diagnostics for raw touch coordinates and pressed state in the display/touch polling path.
3. Collect touch samples at screen corners and center:
: top-left, top-right, bottom-left, bottom-right, center
4. Determine transform needs:
: axis swap, inversion, scaling, and offset
5. Apply minimal mapping changes in the touch handling path.
6. Re-run interactive checks for tap targets near edges and small controls.
7. Remove or gate verbose calibration logs after validation.

## Validation Checklist
- Touch points map correctly at all four corners and center.
- Tap targets activate the intended UI controls.
- No visible lag from touch polling in the main loop.
- Gauge redraws remain stable while touch is active.

## Reporting Format
Return:
- Root cause identified
- Mapping changes applied
- Validation results
- Any remaining hardware assumptions or uncertainties
