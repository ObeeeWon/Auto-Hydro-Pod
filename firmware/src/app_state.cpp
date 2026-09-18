#include "app_state.h"

void AppState::onTelemetry(const Telemetry& t, uint32_t now_ms) {
  last_ = t;
  last_telemetry_ms_ = now_ms;
  has_telemetry_ = true;
  if (!link_up_) {
    link_up_ = true;
    link_changed_ = true;
  }

  // Low water fires on the 0 -> 1 edge only, so a dismissed alarm does not
  // re-open on every 1 Hz heartbeat that still reports water_level = 1.
  const bool low = (t.water_level == 1);
  if (low && !water_low_) {
    alarm_active_ = true;
    alarm_dismissed_ = false;
    alarm_started_ms_ = now_ms;
  } else if (!low) {
    alarm_active_ = false;
    alarm_dismissed_ = false;
  }
  water_low_ = low;

  // Resync the switch to the relay, except while our own command is in flight.
  if (t.has_pump && pending_ != PendingCommand::Pump) {
    pump_on_ = (t.pump == 1);
  }

  // Same idea for the setpoint: the controller owns it, so its echo wins and a
  // panel reboot picks up the real value. The grace window after an accepted
  // command stops the number bouncing if Parker acks before he applies.
  if (t.has_setpoint && pending_ != PendingCommand::Temperature &&
      static_cast<int32_t>(now_ms - setpoint_grace_until_ms_) >= 0) {
    int echoed = static_cast<int>(t.setpoint_c + (t.setpoint_c < 0.0f ? -0.5f : 0.5f));
    if (echoed < cfg::kTempMinC) echoed = cfg::kTempMinC;
    if (echoed > cfg::kTempMaxC) echoed = cfg::kTempMaxC;
    target_temp_c_ = echoed;
    setpoint_known_ = true;
  }

  updateTestButton(t, now_ms);
}

void AppState::updateTestButton(const Telemetry& t, uint32_t now_ms) {
  if (!t.has_test_button && !t.has_test_count) return;
  test_active_ = true;

  // The counter is the real proof: it survives a press that fell between two
  // heartbeats, and a gap in it means frames were lost.
  if (t.has_test_count) {
    if (!test_count_seen_) {
      test_count_ = t.test_count;  // adopt whatever he counted before we booted
      test_count_seen_ = true;
    } else if (t.test_count > test_count_) {
      test_new_presses_ += t.test_count - test_count_;
      test_count_ = t.test_count;
      test_lamp_until_ms_ = now_ms + cfg::kTestLampHoldMs;
    } else if (t.test_count < test_count_) {
      test_count_ = t.test_count;  // controller rebooted
      test_counter_reset_ = true;
    }
  }

  if (t.has_test_button) {
    const bool held = (t.test_button == 1);
    // Without a counter, the press edge is all we have.
    if (held && !test_button_held_ && !t.has_test_count) {
      test_new_presses_++;
      test_lamp_until_ms_ = now_ms + cfg::kTestLampHoldMs;
    }
    test_button_held_ = held;
  }
}

bool AppState::testLampOn(uint32_t now_ms) const {
  if (test_button_held_) return true;
  return static_cast<int32_t>(now_ms - test_lamp_until_ms_) < 0;
}

int AppState::consumeTestPresses() {
  const int n = test_new_presses_;
  test_new_presses_ = 0;
  return n;
}

bool AppState::consumeTestCounterReset() {
  const bool r = test_counter_reset_;
  test_counter_reset_ = false;
  return r;
}

void AppState::onAck(bool ok, uint32_t now_ms) {
  if (pending_ == PendingCommand::None) return;  // unsolicited ack, ignore

  if (ok) {
    if (pending_ == PendingCommand::Pump) {
      pump_on_ = pending_pump_on_;
    } else if (pending_ == PendingCommand::Temperature) {
      target_temp_c_ = pending_temp_c_;
      setpoint_grace_until_ms_ = now_ms + cfg::kSetpointEchoGraceMs;
    }
    result_ = CommandResult::Accepted;
  } else {
    result_ = CommandResult::Rejected;
  }
  clearPending();
}

void AppState::tick(uint32_t now_ms) {
  if (has_telemetry_ && link_up_ &&
      (now_ms - last_telemetry_ms_) >= cfg::kLinkTimeoutMs) {
    link_up_ = false;
    link_changed_ = true;
  }
  if (pending_ != PendingCommand::None &&
      (now_ms - pending_sent_ms_) >= cfg::kAckTimeoutMs) {
    result_ = CommandResult::TimedOut;
    clearPending();
  }
}

bool AppState::requestPump(bool on, uint32_t now_ms) {
  if (pending_ != PendingCommand::None) return false;
  pending_ = PendingCommand::Pump;
  pending_pump_on_ = on;
  pending_sent_ms_ = now_ms;
  return true;
}

bool AppState::requestTemperature(int celsius, uint32_t now_ms) {
  if (pending_ != PendingCommand::None) return false;
  if (celsius < cfg::kTempMinC || celsius > cfg::kTempMaxC) return false;
  pending_ = PendingCommand::Temperature;
  pending_temp_c_ = celsius;
  pending_sent_ms_ = now_ms;
  return true;
}

bool AppState::alarmDismissAllowed(uint32_t now_ms) const {
  if (!alarm_active_) return false;
  return (now_ms - alarm_started_ms_) >= cfg::kAlarmMinFlashMs;
}

bool AppState::dismissAlarm(uint32_t now_ms) {
  if (!alarmDismissAllowed(now_ms)) return false;
  alarm_active_ = false;
  alarm_dismissed_ = true;
  return true;
}

CommandResult AppState::consumeCommandResult() {
  const CommandResult r = result_;
  result_ = CommandResult::None;
  return r;
}

bool AppState::consumeLinkChanged() {
  const bool c = link_changed_;
  link_changed_ = false;
  return c;
}

void AppState::clearPending() {
  pending_ = PendingCommand::None;
  pending_pump_on_ = false;
  pending_temp_c_ = 0;
  pending_sent_ms_ = 0;
}
