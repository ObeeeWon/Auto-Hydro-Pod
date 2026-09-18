#pragma once

#include "app_config.h"

// Raw ADC -> integer percent. Parker sends full-scale 0..4095; the panel is the
// only place that converts. Matches the frozen band: 1844 = 45%, 2457 = 60%.
inline int moisture_to_percent(int raw) {
  if (raw < 0) raw = 0;
  if (raw > cfg::kAdcMax) raw = cfg::kAdcMax;
  return static_cast<int>((static_cast<float>(raw) * 100.0f) / static_cast<float>(cfg::kAdcMax) + 0.5f);
}
