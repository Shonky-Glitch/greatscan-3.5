#pragma once

// Waveshare ESP32-S3-Touch-LCD-3.5 confirmed hardware:
// - ST7796 display controller over SPI
// - FT6336 touch controller over I2C
// - 320x480 resolution
//
// GPIO mapping below is taken from vendor board config in Waveshare's
// ESP32-S3-Touch-LCD-3.5 firmware examples.
//
// Header mapping notes from your pinout:
// - Power rails: pins 1/2 = BAT, pins 31/32 = 3V3
// - Ground rails: pins 3/4/29/30 = GND
// - USB lines on header: pin 6 = DN (GPIO19), pin 8 = DP (GPIO20)
// - Key lines on header: pin 20 = BOOT key (GPIO0), pin 22 = RST key,
//   pin 24 = PWR key
// - I2C lines on header: pin 26 = SCL (GPIO7), pin 28 = SDA (GPIO18)
// - UART lines on header: pin 25 = RXD (GPIO44), pin 27 = TXD (GPIO43)
// - All other header pins are general-purpose GPIO.

namespace WaveshareUiPins
{
constexpr int LCD_CS = -1;   // NC in vendor config
constexpr int LCD_DC = 3;
constexpr int LCD_RST = -1;  // NC in vendor config
constexpr int LCD_BL = 6;
constexpr int LCD_SCLK = 5;
constexpr int LCD_MOSI = 1;
constexpr int LCD_MISO = 2;

constexpr int TOUCH_SDA = 8;
constexpr int TOUCH_SCL = 7;
constexpr int TOUCH_INT = -1;
constexpr int TOUCH_RST = -1;
constexpr int TOUCH_I2C_ADDRESS = 0x38;

constexpr int LCD_WIDTH = 320;
constexpr int LCD_HEIGHT = 480;
}