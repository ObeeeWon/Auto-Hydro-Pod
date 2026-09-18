// LVGL 9.x configuration for the Auto Hydro panel — Arduino IDE builds only.
//
// The PlatformIO build sets LV_CONF_SKIP=1 and passes these same values as -D
// flags, so it never reads this file. The Arduino IDE cannot pass flags, which
// is why this file exists. If you change a value here, change the matching flag
// in platformio.ini too.
//
// Arduino IDE needs this file NEXT TO the lvgl library folder, not in the
// sketch folder:
//
//   macOS   ~/Documents/Arduino/libraries/lv_conf.h
//   Windows Documents\Arduino\libraries\lv_conf.h
//
// i.e. libraries/lv_conf.h sits beside libraries/lvgl/. Copy it there.
//
// Anything not set below falls back to the LVGL default in lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// 16-bit RGB565, matching the panel's parallel RGB bus.
#define LV_COLOR_DEPTH 16

// LVGL's own heap. 64 KB is enough for this UI; the 800x480 framebuffer lives
// in PSRAM and is owned by LovyanGFX, not by LVGL.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64U * 1024U)

// ~30 fps. display_driver.cpp drives the tick through lv_tick_set_cb().
#define LV_DEF_REFR_PERIOD 33

// UART0 is the Parker link — LVGL must never print to it.
#define LV_USE_LOG 0

// ui.cpp uses all four sizes: 14 (log), 20 (labels), 28 (values), 48 (alarm).
// The stock config enables only 14, which is why the old sketch failed to
// compile with "lv_font_montserrat_28 was not declared".
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// We create the display and the flush callback by hand in display_driver.cpp,
// so none of LVGL's bundled display drivers are wanted.
#define LV_USE_TFT_ESPI 0

#endif  // LV_CONF_H
