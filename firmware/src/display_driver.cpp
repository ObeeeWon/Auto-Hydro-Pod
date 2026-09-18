// CrowPanel ESP32 HMI 7.0" (Basic, ASIN B0F8NFFH29) display + touch bring-up.
//
// Pin map is from docs/FEASIBILITY_ASSESSMENT_zh.md §2.1. If the screen stays
// white, verify these against the Elecrow example for your board revision
// before touching anything else — the RGB timing is the usual culprit.
#include "display_driver.h"

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>
// The RGB parallel bus is ESP32-S3 only and is not pulled in by LovyanGFX.hpp.
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

class CrowPanel7 : public lgfx::LGFX_Device {
 public:
  CrowPanel7() {
    {
      auto cfg = panel_.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      panel_.config(cfg);
    }
    {
      // 800x480x16bpp does not fit in internal RAM.
      auto cfg = panel_.config_detail();
      cfg.use_psram = 1;
      panel_.config_detail(cfg);
    }
    {
      auto cfg = bus_.config();
      cfg.panel = &panel_;

      cfg.pin_d0 = GPIO_NUM_15;  // B0
      cfg.pin_d1 = GPIO_NUM_7;   // B1
      cfg.pin_d2 = GPIO_NUM_6;   // B2
      cfg.pin_d3 = GPIO_NUM_5;   // B3
      cfg.pin_d4 = GPIO_NUM_4;   // B4
      cfg.pin_d5 = GPIO_NUM_9;   // G0
      cfg.pin_d6 = GPIO_NUM_46;  // G1
      cfg.pin_d7 = GPIO_NUM_3;   // G2
      cfg.pin_d8 = GPIO_NUM_8;   // G3
      cfg.pin_d9 = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5
      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      cfg.pin_henable = GPIO_NUM_41;  // DE
      cfg.pin_vsync = GPIO_NUM_40;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;      // also the BOOT strap pin
      cfg.freq_write = 15000000;

      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;

      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = GPIO_NUM_2;
      light_.config(cfg);
      panel_.light(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = 799;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;  // I2C1; I2C0 is free for anything Parker adds later
      cfg.pin_sda = GPIO_NUM_19;
      cfg.pin_scl = GPIO_NUM_20;
      cfg.freq = 400000;
      cfg.i2c_addr = 0x5D;  // some panels answer on 0x14
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_RGB bus_;
  lgfx::Panel_RGB panel_;
  lgfx::Light_PWM light_;
  lgfx::Touch_GT911 touch_;
};

CrowPanel7 gfx;

// Partial-render staging buffers. The RGB panel keeps its own full framebuffer
// in PSRAM, so these only need to be a few slices tall; keeping them in
// internal DMA RAM avoids PSRAM stalls during flush.
constexpr int32_t kHRes = 800;
constexpr int32_t kVRes = 480;
constexpr int32_t kBufLines = 24;
constexpr size_t kBufBytes = static_cast<size_t>(kHRes) * kBufLines * 2;

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels(reinterpret_cast<lgfx::rgb565_t*>(px_map),
                  static_cast<int32_t>(w) * h);
  gfx.endWrite();
  lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  int32_t x = 0;
  int32_t y = 0;
  if (gfx.getTouch(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

uint32_t lvglTickCb() { return millis(); }

}  // namespace

bool hmiDisplayBegin() {
  if (!gfx.init()) return false;
  gfx.setRotation(0);
  gfx.fillScreen(TFT_BLACK);
  gfx.setBrightness(0);  // raise it after the first frame to avoid a white flash

  lv_init();
  lv_tick_set_cb(lvglTickCb);

  auto* buf1 = static_cast<uint8_t*>(
      heap_caps_malloc(kBufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  auto* buf2 = static_cast<uint8_t*>(
      heap_caps_malloc(kBufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (buf1 == nullptr) return false;  // buf2 may fail; single buffering still works

  lv_display_t* disp = lv_display_create(kHRes, kVRes);
  if (disp == nullptr) return false;
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, buf1, buf2, kBufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  return true;
}

void hmiSetBrightness(uint8_t level) { gfx.setBrightness(level); }
