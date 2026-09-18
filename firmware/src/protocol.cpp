#include "protocol.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "app_config.h"

namespace {

int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Bring-up tolerance: Parker's earlier draft used humidity_* names. The frozen
// protocol is moisture_*; both are accepted while his firmware catches up.
JsonVariantConst pick(JsonDocument& doc, const char* primary, const char* legacy) {
  JsonVariantConst v = doc[primary];
  if (!v.isNull()) return v;
  return doc[legacy];
}

}  // namespace

bool parseMessage(const char* json, size_t len, InboundMessage& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;

  const char* type = doc["type"].as<const char*>();
  if (type == nullptr) return false;

  if (std::strcmp(type, "telemetry") == 0) {
    Telemetry t{};

    JsonVariantConst raw = pick(doc, "moisture_raw", "humidity_raw");
    JsonVariantConst alert = pick(doc, "moisture_alert", "humidity_alert");
    if (raw.isNull()) return false;  // moisture is mandatory in v1

    t.moisture_raw = clampInt(raw.as<int>(), 0, cfg::kAdcMax);
    t.moisture_alert = clampInt(alert.isNull() ? 0 : alert.as<int>(), 0, 2);
    t.water_level = (doc["water_level"].as<int>() == 1) ? 1 : 0;

    JsonVariantConst temp = doc["temperature_c"];
    if (!temp.isNull()) {
      t.temperature_c = temp.as<float>();
      t.has_temperature = true;
    }

    JsonVariantConst pump = doc["pump"];
    if (!pump.isNull()) {
      t.pump = (pump.as<int>() == 1) ? 1 : 0;
      t.has_pump = true;
    }

    // The setpoint the controller actually holds. Without it the panel would
    // show its own default after a reboot and silently disagree with the box.
    JsonVariantConst setpoint = doc["setpoint_c"];
    if (!setpoint.isNull()) {
      t.setpoint_c = setpoint.as<float>();
      t.has_setpoint = true;
    }

    JsonVariantConst test_button = doc["test_button"];
    if (!test_button.isNull()) {
      t.test_button = (test_button.as<int>() == 1) ? 1 : 0;
      t.has_test_button = true;
    }

    JsonVariantConst test_count = doc["test_count"];
    if (!test_count.isNull()) {
      t.test_count = test_count.as<int>();
      if (t.test_count < 0) t.test_count = 0;
      t.has_test_count = true;
    }

    out.kind = MessageKind::Telemetry;
    out.telemetry = t;
    return true;
  }

  if (std::strcmp(type, "ack") == 0) {
    out.kind = MessageKind::Ack;
    out.ack_ok = doc["ok"].as<bool>();
    return true;
  }

  return false;
}

size_t buildPumpCommand(char* out, size_t cap, bool on) {
  const int code = on ? 1 : 2;  // 1 = on, 2 = off
  const int n = std::snprintf(out, cap, "{\"type\":\"command\",\"pump\":%d}\n", code);
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

size_t buildTemperatureCommand(char* out, size_t cap, int celsius) {
  if (celsius < cfg::kTempMinC || celsius > cfg::kTempMaxC) return 0;
  const int n = std::snprintf(
      out, cap,
      "{\"type\":\"command\",\"temperature\":{\"changed\":1,\"new_temp\":%d}}\n",
      celsius);
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}
