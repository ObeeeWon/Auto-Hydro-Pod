#pragma once

#include <cstdint>

#include "app_config.h"
#include "types.h"

// All panel logic that is not drawing: link supervision, confirm/ack
// bookkeeping, and the low-water alarm edge. No Arduino or LVGL dependency, so
// it builds and gets tested natively.
class AppState {
 public:
  // --- inbound ----------------------------------------------------------
  void onTelemetry(const Telemetry& t, uint32_t now_ms);
  void onAck(bool ok, uint32_t now_ms);
  void tick(uint32_t now_ms);

  // --- operator actions (called after the confirm dialog) ---------------
  // Returns false when a command is already awaiting an ack.
  bool requestPump(bool on, uint32_t now_ms);
  bool requestTemperature(int celsius, uint32_t now_ms);

  // --- alarm ------------------------------------------------------------
  bool alarmDismissAllowed(uint32_t now_ms) const;
  bool dismissAlarm(uint32_t now_ms);

  // --- queries ----------------------------------------------------------
  bool linkUp() const { return link_up_; }
  bool hasTelemetry() const { return has_telemetry_; }
  const Telemetry& telemetry() const { return last_; }

  bool pumpOn() const { return pump_on_; }
  int targetTemperatureC() const { return target_temp_c_; }
  // False means the shown setpoint is the panel's own default, never confirmed
  // by the controller.
  bool setpointKnown() const { return setpoint_known_; }

  PendingCommand pending() const { return pending_; }
  bool pendingPumpOn() const { return pending_pump_on_; }
  int pendingTemperatureC() const { return pending_temp_c_; }

  bool waterLow() const { return water_low_; }
  bool alarmActive() const { return alarm_active_; }
  uint32_t alarmStartedMs() const { return alarm_started_ms_; }

  // --- bring-up test button ---------------------------------------------
  // Inactive until the controller sends a test field, so the indicator is
  // absent in production without needing a build flag.
  bool testActive() const { return test_active_; }
  int testCount() const { return test_count_; }
  bool testLampOn(uint32_t now_ms) const;
  int consumeTestPresses();
  bool consumeTestCounterReset();

  // Read-and-clear so the UI logs each outcome exactly once.
  CommandResult consumeCommandResult();
  bool consumeLinkChanged();

 private:
  void clearPending();
  void updateTestButton(const Telemetry& t, uint32_t now_ms);

  bool has_telemetry_ = false;
  uint32_t last_telemetry_ms_ = 0;
  bool link_up_ = false;
  bool link_changed_ = false;
  Telemetry last_{};

  bool pump_on_ = false;
  int target_temp_c_ = cfg::kTempDefaultC;  // until the controller echoes setpoint_c
  bool setpoint_known_ = false;
  uint32_t setpoint_grace_until_ms_ = 0;

  PendingCommand pending_ = PendingCommand::None;
  bool pending_pump_on_ = false;
  int pending_temp_c_ = 0;
  uint32_t pending_sent_ms_ = 0;
  CommandResult result_ = CommandResult::None;

  bool test_active_ = false;
  bool test_button_held_ = false;
  bool test_count_seen_ = false;
  bool test_counter_reset_ = false;
  int test_count_ = 0;
  int test_new_presses_ = 0;
  uint32_t test_lamp_until_ms_ = 0;

  bool water_low_ = false;
  bool alarm_active_ = false;
  bool alarm_dismissed_ = false;
  uint32_t alarm_started_ms_ = 0;
};
