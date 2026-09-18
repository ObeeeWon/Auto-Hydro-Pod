#pragma once

#include <cstdint>

// Brings up the CrowPanel 7" RGB panel + GT911 touch and registers both with
// LVGL. Call once, before ui::init().
bool hmiDisplayBegin();

// 0..255. The TN panel is dim at low values; keep >= 120 in a lit room.
void hmiSetBrightness(uint8_t level);
