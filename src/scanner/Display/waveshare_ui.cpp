#include "waveshare_ui.h"

#include <cstdio>
#include <Wire.h>
#include <lvgl.h>

#if __has_include(<esp32-hal-periman.h>)
#define WAVESHARE_UI_GFX_AVAILABLE 1
#include <Arduino_GFX_Library.h>
#else
#define WAVESHARE_UI_GFX_AVAILABLE 0
#endif

#include "waveshare_board_config.h"

namespace
{
struct TouchPoint
{
  uint16_t x = 0;
  uint16_t y = 0;
  bool active = false;
};

#if WAVESHARE_UI_GFX_AVAILABLE
Arduino_DataBus* displayBus = nullptr;
Arduino_GFX* display = nullptr;
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_color_t* drawPixels = nullptr;
lv_obj_t* titleLabel = nullptr;
lv_obj_t* pageLabel = nullptr;
lv_obj_t* bodyLabel = nullptr;
lv_obj_t* footerLabel = nullptr;
#endif
bool uiReady = false;
bool uiConfigWarningPrinted = false;
bool touchReady = false;
bool touchPressed = false;
unsigned long lastTouchMillis = 0;
unsigned long lastLvglTickMillis = 0;

bool pinsConfigured()
{
  return WaveshareUiPins::LCD_DC >= 0 &&
         WaveshareUiPins::LCD_SCLK >= 0 &&
         WaveshareUiPins::LCD_MOSI >= 0;
}

bool touchPinsConfigured()
{
  return WaveshareUiPins::TOUCH_SDA >= 0 &&
         WaveshareUiPins::TOUCH_SCL >= 0;
}

bool readTouchPoint(TouchPoint& point)
{
  if (!touchReady)
  {
    return false;
  }

  Wire.beginTransmission(WaveshareUiPins::TOUCH_I2C_ADDRESS);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  if (Wire.requestFrom(WaveshareUiPins::TOUCH_I2C_ADDRESS, 5) != 5)
  {
    return false;
  }

  const uint8_t touchCount = Wire.read() & 0x0F;
  if (touchCount == 0)
  {
    point.active = false;
    return true;
  }

  const uint8_t xh = Wire.read();
  const uint8_t xl = Wire.read();
  const uint8_t yh = Wire.read();
  const uint8_t yl = Wire.read();

  point.x = static_cast<uint16_t>(((xh & 0x0F) << 8) | xl);
  point.y = static_cast<uint16_t>(((yh & 0x0F) << 8) | yl);
  point.active = true;
  return true;
}

void printUiNotConfigured(Stream& logStream)
{
  if (!uiConfigWarningPrinted)
  {
    logStream.println("[UI] Waveshare ST7796/FT6336 support is compiled in, but the board pin map is not configured yet.");
    logStream.println("[UI] Fill WaveshareUiPins in src/Display/waveshare_board_config.h to enable the LCD backend.");
    uiConfigWarningPrinted = true;
  }
}

void printUiBackendUnavailable(Stream& logStream)
{
  if (!uiConfigWarningPrinted)
  {
    logStream.println("[UI] Arduino_GFX backend disabled: installed Arduino core is missing esp32-hal-periman.h.");
    logStream.println("[UI] Keep diagnostics running over serial or update board/framework to a compatible core.");
    uiConfigWarningPrinted = true;
  }
}

#if WAVESHARE_UI_GFX_AVAILABLE
void lvglFlushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP)
{
  if (!display)
  {
    lv_disp_flush_ready(disp);
    return;
  }

  const int32_t x = area->x1;
  const int32_t y = area->y1;
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  display->draw16bitRGBBitmap(x, y, reinterpret_cast<uint16_t*>(colorP), width, height);
  lv_disp_flush_ready(disp);
}

void serviceLvgl()
{
  const unsigned long now = millis();
  if (lastLvglTickMillis == 0)
  {
    lastLvglTickMillis = now;
  }

  const unsigned long elapsed = now - lastLvglTickMillis;
  if (elapsed > 0)
  {
    lv_tick_inc(elapsed);
    lastLvglTickMillis = now;
  }

  lv_timer_handler();
}

void createLvglScreen()
{
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  titleLabel = lv_label_create(screen);
  lv_label_set_text(titleLabel, "Great Scan");
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), LV_PART_MAIN);

  pageLabel = lv_label_create(screen);
  lv_label_set_text(pageLabel, "Page: Overview");
  lv_obj_align(pageLabel, LV_ALIGN_TOP_LEFT, 10, 30);
  lv_obj_set_style_text_color(pageLabel, lv_color_white(), LV_PART_MAIN);

  bodyLabel = lv_label_create(screen);
  lv_obj_set_width(bodyLabel, WaveshareUiPins::LCD_WIDTH - 20);
  lv_obj_align(bodyLabel, LV_ALIGN_TOP_LEFT, 10, 46);
  lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(bodyLabel, lv_color_white(), LV_PART_MAIN);

  footerLabel = lv_label_create(screen);
  lv_label_set_text(footerLabel, "Touch left | right | refresh");
  lv_obj_align(footerLabel, LV_ALIGN_BOTTOM_LEFT, 10, -2);
  lv_obj_set_style_text_color(footerLabel, lv_color_white(), LV_PART_MAIN);
}
#endif
}

bool DisplayManager::begin(Stream& logStream)
{
#if !WAVESHARE_UI_GFX_AVAILABLE
  printUiBackendUnavailable(logStream);
  uiReady = false;
  return false;
#else
  if (!pinsConfigured())
  {
    printUiNotConfigured(logStream);
    uiReady = false;
    return false;
  }

  displayBus = new Arduino_ESP32SPI(
    WaveshareUiPins::LCD_DC,
    WaveshareUiPins::LCD_CS,
    WaveshareUiPins::LCD_SCLK,
    WaveshareUiPins::LCD_MOSI,
    WaveshareUiPins::LCD_MISO
  );
  display = new Arduino_ST7796(
    displayBus,
    WaveshareUiPins::LCD_RST,
    0,
    true,
    WaveshareUiPins::LCD_WIDTH,
    WaveshareUiPins::LCD_HEIGHT
  );

  if (!display || !display->begin())
  {
    logStream.println("[UI] Waveshare display initialization failed.");
    uiReady = false;
    return false;
  }

  if (WaveshareUiPins::LCD_BL >= 0)
  {
    pinMode(WaveshareUiPins::LCD_BL, OUTPUT);
    digitalWrite(WaveshareUiPins::LCD_BL, HIGH);
  }

  if (touchPinsConfigured())
  {
    if (WaveshareUiPins::TOUCH_RST >= 0)
    {
      pinMode(WaveshareUiPins::TOUCH_RST, OUTPUT);
      digitalWrite(WaveshareUiPins::TOUCH_RST, HIGH);
    }
    if (WaveshareUiPins::TOUCH_INT >= 0)
    {
      pinMode(WaveshareUiPins::TOUCH_INT, INPUT_PULLUP);
    }
    Wire.begin(WaveshareUiPins::TOUCH_SDA, WaveshareUiPins::TOUCH_SCL);
    touchReady = true;
    logStream.println("[UI] FT6336 touch polling enabled.");
  }

  display->fillScreen(BLACK);

  lv_init();
  const int drawRows = 40;
  const size_t pixelCount = static_cast<size_t>(WaveshareUiPins::LCD_WIDTH) * static_cast<size_t>(drawRows);
  drawPixels = static_cast<lv_color_t*>(heap_caps_malloc(pixelCount * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!drawPixels)
  {
    logStream.println("[UI] LVGL draw buffer allocation failed.");
    uiReady = false;
    return false;
  }

  lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, static_cast<uint32_t>(pixelCount));
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = WaveshareUiPins::LCD_WIDTH;
  displayDriver.ver_res = WaveshareUiPins::LCD_HEIGHT;
  displayDriver.flush_cb = lvglFlushCallback;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  createLvglScreen();
  serviceLvgl();

  uiReady = true;
  logStream.println("[UI] Waveshare display backend ready (LVGL).");
  return true;
#endif
}

bool DisplayManager::available() const
{
  return uiReady;
}

void DisplayManager::render(const DisplayFrame& frame, Stream& logStream)
{
#if !WAVESHARE_UI_GFX_AVAILABLE
  printUiBackendUnavailable(logStream);
  (void)frame;
  return;
#else
  if (!uiReady)
  {
    printUiNotConfigured(logStream);
    return;
  }

  serviceLvgl();
  lv_label_set_text_fmt(pageLabel, "Page: %s", frame.pageName);

  char bodyText[512] = {};

  switch (frame.currentPage)
  {
    case DisplayPage::Overview:
      snprintf(
        bodyText,
        sizeof(bodyText),
        "Diag: %s\nCAN1: %s %s\nCAN2: %s %s\nVIN: %s\nDTCs: %u Last:%s\n%s",
        frame.diagnosticBus,
        frame.can1Started ? "up" : "down",
        frame.can1Bitrate,
        frame.can2Started ? "up" : "down",
        frame.can2Bitrate,
        frame.vin,
        frame.dtcCount,
        frame.lastDtc,
        frame.status
      );
      break;
    case DisplayPage::LivePids:
    {
      size_t offset = 0;
      for (size_t index = 0; index < DISPLAY_PID_SLOTS; ++index)
      {
        if (!frame.pids[index].valid)
        {
          continue;
        }
        if (offset >= sizeof(bodyText))
        {
          break;
        }
        const int written = snprintf(
          bodyText + offset,
          sizeof(bodyText) - offset,
          "%s\n%s\n",
          frame.pids[index].label,
          frame.pids[index].value
        );
        if (written <= 0)
        {
          break;
        }
        offset += static_cast<size_t>(written);
      }
      break;
    }
    case DisplayPage::VehicleInfo:
      snprintf(bodyText, sizeof(bodyText), "VIN: %s\n%s\nTap left/right to page", frame.vin, frame.status);
      break;
    case DisplayPage::Faults:
      snprintf(bodyText, sizeof(bodyText), "DTC count: %u\nLatest: %s\n%s", frame.dtcCount, frame.lastDtc, frame.status);
      break;
    case DisplayPage::Help:
      snprintf(bodyText, sizeof(bodyText), "Top left: prev page\nTop right: next page\nBottom: refresh\nSerial: m t 1 2 a s");
      break;
  }

  lv_label_set_text(bodyLabel, bodyText);
  serviceLvgl();
#endif
}

WaveshareUiAction DisplayManager::pollAction(Stream& logStream)
{
#if WAVESHARE_UI_GFX_AVAILABLE
  serviceLvgl();
#endif

  if (!uiReady || !touchReady)
  {
    return WaveshareUiAction::None;
  }

  TouchPoint point {};
  if (!readTouchPoint(point))
  {
    return WaveshareUiAction::None;
  }

  if (!point.active)
  {
    touchPressed = false;
    return WaveshareUiAction::None;
  }

  const unsigned long now = millis();
  if (touchPressed && now - lastTouchMillis < 250)
  {
    return WaveshareUiAction::None;
  }

  touchPressed = true;
  lastTouchMillis = now;

  if (point.y >= static_cast<uint16_t>(WaveshareUiPins::LCD_HEIGHT - 60))
  {
    logStream.println("[UI] Touch action: refresh");
    return WaveshareUiAction::Refresh;
  }

  if (point.x < static_cast<uint16_t>(WaveshareUiPins::LCD_WIDTH / 2))
  {
    logStream.println("[UI] Touch action: previous page");
    return WaveshareUiAction::PreviousPage;
  }

  logStream.println("[UI] Touch action: next page");
  return WaveshareUiAction::NextPage;
}