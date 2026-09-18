#pragma once

#include <cstdint>

// One decoded telemetry frame. Optional fields carry a has_* flag so the UI can
// grey out values Parker did not send instead of showing a stale number.
struct Telemetry {
  int moisture_raw = 0;      // 0..4095, raw 12-bit ADC
  int moisture_alert = 0;    // 0 = in band, 1 = too wet, 2 = too dry
  int water_level = 0;       // 0 = ok, 1 = low water
  float temperature_c = 0.0f;
  bool has_temperature = false;
  int pump = 0;              // actual pump state, 0 = off, 1 = on
  bool has_pump = false;
  float setpoint_c = 0.0f;   // setpoint the controller is actually holding
  bool has_setpoint = false;

  // Bring-up only. Parker wires a momentary button to a spare GPIO so one press
  // proves button -> controller -> UART -> panel end to end. Both optional; the
  // whole test row disappears from the screen when they stop arriving.
  int test_button = 0;       // 1 while held
  bool has_test_button = false;
  int test_count = 0;        // monotonic press counter, survives a missed frame
  bool has_test_count = false;
};

enum class MoistureAlert : int {
  Ok = 0,
  TooWet = 1,
  TooDry = 2,
};

// Outcome of the last command the panel put on the wire.
enum class CommandResult : uint8_t {
  None = 0,
  Accepted,   // ack ok:true
  Rejected,   // ack ok:false
  TimedOut,   // no ack within kAckTimeoutMs
};

enum class PendingCommand : uint8_t {
  None = 0,
  Pump,
  Temperature,
};
