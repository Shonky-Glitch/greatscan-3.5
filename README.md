# GreatScan 3.5 — Homemade ScanGauge3-Style Gauge Cluster (Ford Ranger PX2)

A fully functional, homemade ScanGauge3-style digital gauge cluster built for a
Ford Ranger PX2. It reads live vehicle data over CAN, decodes it, and displays
it on a touchscreen gauge UI — the same idea as a commercial ScanGauge, but
custom-built on ESP32-S3 hardware.

- Touchscreen gauge UI (RPM, coolant, speed, boost, fuel, throttle, oil
  pressure, AFR, oil temp, battery voltage, ATF/EGT temps, O2 sensor voltage)
- Live CAN decoding of Ford PX2 Ranger data via a dedicated scanner board
- Wi-Fi web dashboard mirroring the same live gauge data

This workspace hosts firmware for two separate physical boards, linked over
ESP-NOW/UART, in one PlatformIO project:

| Environment | Board | Port | Role |
|---|---|---|---|
| `esp32-s3-ui` | Waveshare ESP32-S3-Touch-LCD-3.5 | COM9 | Touchscreen gauge UI / mirror |
| `esp32-can-x2` | Autosport Labs ESP32-CAN-X2 | COM8 | CAN1/CAN2 diagnostic scanner |

Each environment's `build_src_filter` in `platformio.ini` restricts it to its
own source tree (`src/**` for the UI, `src/scanner/**` for the scanner), so
building/uploading one never touches the other's code or board.

## Current Target

- PlatformIO environment: `esp32-s3-ui`
- Board profile: `esp32-s3-devkitc-1` (Waveshare ESP32-S3-Touch-LCD-3.5 hardware)

## Run

```bash
# UI board (Waveshare ESP32-S3 Touch, COM9)
pio run --environment esp32-s3-ui
pio run --environment esp32-s3-ui --target upload --upload-port COM9
pio device monitor --environment esp32-s3-ui --port COM9 --baud 115200

# CAN-X2 scanner board (Autosport Labs ESP32-CAN-X2, COM8)
pio run --environment esp32-can-x2
pio run --environment esp32-can-x2 --target upload --upload-port COM8
pio device monitor --environment esp32-can-x2 --port COM8 --baud 115200
```

## Possible Next Steps

1. Expand the web API from `/api/gauges` to include DTCs and additional sensor channels.
2. Add support for additional vehicle protocols beyond Ford/Toyota in `lib/Vehicle`.
3. Add on-device data logging / trip history.