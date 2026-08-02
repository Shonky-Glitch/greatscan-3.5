#pragma once

// Update these values after matching your exact Waveshare board wiring.
// The product name is "Waveshare ESP32-S3-Touch-LCD-3.5" but PlatformIO
// generally uses a generic ESP32-S3 board profile unless a custom one is added.

namespace ProjectConfig {
constexpr const char* kDeviceName = "GreatScan 3.5";

// Wi-Fi AP fallback credentials.
constexpr const char* kApSsid = "GreatScan-3.5";
constexpr const char* kApPassword = "12345678";
// Keep AP subnet different from Great Scan AP (192.168.4.1) to avoid AP/STA IP collisions.
constexpr uint8_t kApIpA = 192;
constexpr uint8_t kApIpB = 168;
constexpr uint8_t kApIpC = 8;
constexpr uint8_t kApIpD = 1;
constexpr uint8_t kApSubnetA = 255;
constexpr uint8_t kApSubnetB = 255;
constexpr uint8_t kApSubnetC = 255;
constexpr uint8_t kApSubnetD = 0;

// Station mode credentials (set before use).
constexpr const char* kStaSsid = "GreatScan-WiFi";
constexpr const char* kStaPassword = "greatscan";

// Great Scan Wi-Fi endpoint consumed by this interface node.
constexpr const char* kGreatScanStatusUrl = "http://192.168.4.1/api/status";
constexpr const char* kGreatScanRawUrl = "http://192.168.4.1/api/raw";
constexpr const char* kGreatScanCommandUrl = "http://192.168.4.1/cmd?c=";
constexpr unsigned long kGreatScanStatusPollMs = 120;
constexpr unsigned long kGreatScanRawPollMs = 700;

// Display conversion for fuel remaining.
constexpr float kFuelTankLiters = 80.0f;

// Optional wired bridge (Great Scan TX -> this board RX + shared GND).
constexpr bool kGreatScanSerialBridgeEnabled = false;
constexpr uint32_t kGreatScanSerialBridgeBaud = 115200;
constexpr int kGreatScanSerialBridgeRxPin = 44;
constexpr int kGreatScanSerialBridgeTxPin = 43;

// Waveshare ESP32-S3-Touch-LCD-3.5 confirmed display wiring.
constexpr int kLcdCsPin = -1;
constexpr int kLcdDcPin = 3;
constexpr int kLcdRstPin = -1;
constexpr int kLcdBacklightPin = 6;
constexpr int kLcdSclkPin = 5;
constexpr int kLcdMosiPin = 1;
constexpr int kLcdMisoPin = 2;
constexpr int kLcdWidth = 320;
constexpr int kLcdHeight = 480;
constexpr uint8_t kLcdResetExpanderAddress = 0x20;
constexpr uint8_t kLcdResetExpanderPinMask = 0x03;

// FT6336 touch controller on Waveshare ESP32-S3-Touch-LCD-3.5.
constexpr int kTouchSdaPin = 8;
constexpr int kTouchSclPin = 7;
constexpr int kTouchI2cAddress = 0x38;
constexpr bool kTouchSwapXY = true;
constexpr bool kTouchInvertX = true;
constexpr bool kTouchInvertY = false;
constexpr int kTouchRawMaxX = 320;
constexpr int kTouchRawMaxY = 480;
constexpr unsigned long kTouchPollMs = 25;

// Optional/board-specific GPIOs.
constexpr int kTouchIntPin = -1;
constexpr int kBacklightPin = -1;
}
