#include "mock_link.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_config.h"

namespace {

// Walks in and out of the 45-60% band so every moisture state gets exercised.
constexpr int kMoistureWalk[] = {2300, 2400, 2500, 2350, 2000, 1800, 1900, 2200};
constexpr size_t kMoistureWalkLen = sizeof(kMoistureWalk) / sizeof(kMoistureWalk[0]);
constexpr uint32_t kSamplePeriodMs = 5000;
constexpr uint32_t kTelemetryPeriodMs = 1000;
constexpr uint32_t kAckDelayMs = 200;

}  // namespace

void MockLink::begin(uint32_t now_ms) {
  start_ms_ = now_ms;
  next_telemetry_ms_ = now_ms;
  next_sample_ms_ = now_ms + kSamplePeriodMs;
  step_ = 0;
  raw_ = kMoistureWalk[0];
}

int MockLink::alertFor(int raw) const {
  if (raw > cfg::kMoistureRawHigh) return 1;  // too wet
  if (raw < cfg::kMoistureRawLow) return 2;   // too dry
  return 0;
}

bool MockLink::waterLowAt(uint32_t elapsed_ms) const {
  const uint32_t s = elapsed_ms / 1000;
  return (s >= 40 && s < 65) || (s >= 150 && s < 170);
}

bool MockLink::testButtonAt(uint32_t elapsed_ms) const {
  // Short presses on purpose: 250 ms is well under the 1 Hz heartbeat, which is
  // exactly the case the press counter has to cover. 25 s and 26 s are a double
  // press, so the counter visibly jumps by two.
  struct Window { uint32_t from, to; };
  static const Window kPresses[] = {
      {12000, 12250}, {25000, 25250}, {26000, 26250}, {45000, 45400}};
  for (const Window& w : kPresses) {
    if (elapsed_ms >= w.from && elapsed_ms < w.to) return true;
  }
  return false;
}

bool MockLink::poll(uint32_t now_ms, char* out, size_t cap, size_t& len) {
  if (ack_queued_ && static_cast<int32_t>(now_ms - ack_due_ms_) >= 0) {
    ack_queued_ = false;
    const int n = std::snprintf(out, cap, "{\"type\":\"ack\",\"ok\":true}\n");
    if (n <= 0) return false;
    len = static_cast<size_t>(n);
    return true;
  }

  if (static_cast<int32_t>(now_ms - next_sample_ms_) >= 0) {
    next_sample_ms_ = now_ms + kSamplePeriodMs;
    step_ = (step_ + 1) % kMoistureWalkLen;
    raw_ = kMoistureWalk[step_];
  }

  const uint32_t elapsed = now_ms - start_ms_;
  const int water = waterLowAt(elapsed) ? 1 : 0;
  const bool water_edge = (water != water_);
  water_ = water;

  const int button = testButtonAt(elapsed) ? 1 : 0;
  const bool button_edge = (button != test_button_);
  if (button == 1 && test_button_ == 0) test_count_++;
  test_button_ = button;

  // Edges get an immediate frame; a 250 ms press would otherwise fall between
  // two heartbeats. Parker's firmware has to do the same.
  if (!water_edge && !button_edge &&
      static_cast<int32_t>(now_ms - next_telemetry_ms_) < 0) {
    return false;
  }
  next_telemetry_ms_ = now_ms + kTelemetryPeriodMs;

  const float temp = 21.0f + 2.5f * std::sin(static_cast<float>(elapsed) / 20000.0f);
  const int n = std::snprintf(out, cap,
                             "{\"type\":\"telemetry\",\"moisture_raw\":%d,"
                             "\"moisture_alert\":%d,\"water_level\":%d,"
                             "\"temperature_c\":%.1f,\"setpoint_c\":%d,\"pump\":%d,"
                             "\"test_button\":%d,\"test_count\":%d}\n",
                             raw_, alertFor(raw_), water_,
                             static_cast<double>(temp), setpoint_, pump_,
                             test_button_, test_count_);
  if (n <= 0 || static_cast<size_t>(n) >= cap) return false;
  len = static_cast<size_t>(n);
  return true;
}

void MockLink::onCommand(const char* line, size_t len, uint32_t now_ms) {
  (void)len;
  if (std::strstr(line, "\"pump\":1") != nullptr) pump_ = 1;
  if (std::strstr(line, "\"pump\":2") != nullptr) pump_ = 0;
  const char* temp_field = std::strstr(line, "\"new_temp\":");
  if (temp_field != nullptr) {
    int value = 0;
    if (std::sscanf(temp_field + 11, "%d", &value) == 1) setpoint_ = value;
  }
  ack_queued_ = true;
  ack_due_ms_ = now_ms + kAckDelayMs;
}
