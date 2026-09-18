// Host tests for the hardware-independent layers. Run with: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "app_config.h"
#include "app_state.h"
#include "line_assembler.h"
#include "mock_link.h"
#include "moisture.h"
#include "protocol.h"

void setUp() {}
void tearDown() {}

// --- moisture scaling ---------------------------------------------------
void test_moisture_percent_band() {
  TEST_ASSERT_EQUAL_INT(0, moisture_to_percent(0));
  TEST_ASSERT_EQUAL_INT(100, moisture_to_percent(cfg::kAdcMax));
  TEST_ASSERT_EQUAL_INT(cfg::kMoisturePctLow, moisture_to_percent(cfg::kMoistureRawLow));
  TEST_ASSERT_EQUAL_INT(cfg::kMoisturePctHigh, moisture_to_percent(cfg::kMoistureRawHigh));
}

void test_moisture_percent_clamps() {
  TEST_ASSERT_EQUAL_INT(0, moisture_to_percent(-500));
  TEST_ASSERT_EQUAL_INT(100, moisture_to_percent(99999));
}

// --- line splitting -----------------------------------------------------
static std::vector<std::string> split(const char* stream) {
  LineAssembler<cfg::kMaxLineLen> rx;
  std::vector<std::string> out;
  for (const char* p = stream; *p != '\0'; ++p) {
    if (rx.push(*p)) out.emplace_back(rx.line(), rx.line_len());
  }
  return out;
}

void test_line_assembler_lf_and_crlf() {
  const auto lines = split("{\"a\":1}\n{\"b\":2}\r\n");
  TEST_ASSERT_EQUAL_size_t(2, lines.size());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING("{\"b\":2}", lines[1].c_str());
}

void test_line_assembler_skips_blank_lines() {
  const auto lines = split("\n\n{\"a\":1}\n\n");
  TEST_ASSERT_EQUAL_size_t(1, lines.size());
}

void test_line_assembler_drops_overlong_line() {
  std::string stream(cfg::kMaxLineLen * 2, 'x');
  stream += "\n{\"a\":1}\n";
  const auto lines = split(stream.c_str());
  TEST_ASSERT_EQUAL_size_t(1, lines.size());  // garbage dropped, next line intact
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", lines[0].c_str());
}

// --- protocol -----------------------------------------------------------
static bool parse(const char* json, InboundMessage& msg) {
  return parseMessage(json, std::strlen(json), msg);
}

void test_parse_full_telemetry() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":2300,"
                         "\"moisture_alert\":0,\"water_level\":1,"
                         "\"temperature_c\":22.5,\"pump\":1}",
                         msg));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MessageKind::Telemetry), static_cast<int>(msg.kind));
  TEST_ASSERT_EQUAL_INT(2300, msg.telemetry.moisture_raw);
  TEST_ASSERT_EQUAL_INT(1, msg.telemetry.water_level);
  TEST_ASSERT_TRUE(msg.telemetry.has_temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, msg.telemetry.temperature_c);
  TEST_ASSERT_TRUE(msg.telemetry.has_pump);
  TEST_ASSERT_EQUAL_INT(1, msg.telemetry.pump);
}

void test_parse_marks_missing_optional_fields() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":1500,\"water_level\":0}", msg));
  TEST_ASSERT_FALSE(msg.telemetry.has_temperature);
  TEST_ASSERT_FALSE(msg.telemetry.has_pump);
  TEST_ASSERT_FALSE(msg.telemetry.has_setpoint);
  TEST_ASSERT_EQUAL_INT(0, msg.telemetry.moisture_alert);
}

void test_parse_test_button_fields() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":1500,\"water_level\":0,"
                         "\"test_button\":1,\"test_count\":12}",
                         msg));
  TEST_ASSERT_TRUE(msg.telemetry.has_test_button);
  TEST_ASSERT_EQUAL_INT(1, msg.telemetry.test_button);
  TEST_ASSERT_TRUE(msg.telemetry.has_test_count);
  TEST_ASSERT_EQUAL_INT(12, msg.telemetry.test_count);

  InboundMessage plain;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":1500,\"water_level\":0}", plain));
  TEST_ASSERT_FALSE(plain.telemetry.has_test_button);
  TEST_ASSERT_FALSE(plain.telemetry.has_test_count);
}

void test_parse_setpoint() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":1500,\"water_level\":0,"
                         "\"setpoint_c\":26}",
                         msg));
  TEST_ASSERT_TRUE(msg.telemetry.has_setpoint);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.0f, msg.telemetry.setpoint_c);
}

void test_parse_accepts_legacy_humidity_names() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"humidity_raw\":2000,"
                         "\"humidity_alert\":2,\"water_level\":0}",
                         msg));
  TEST_ASSERT_EQUAL_INT(2000, msg.telemetry.moisture_raw);
  TEST_ASSERT_EQUAL_INT(2, msg.telemetry.moisture_alert);
}

void test_parse_clamps_out_of_range_values() {
  InboundMessage msg;
  TEST_ASSERT_TRUE(parse("{\"type\":\"telemetry\",\"moisture_raw\":9000,"
                         "\"moisture_alert\":7,\"water_level\":5}",
                         msg));
  TEST_ASSERT_EQUAL_INT(cfg::kAdcMax, msg.telemetry.moisture_raw);
  TEST_ASSERT_EQUAL_INT(2, msg.telemetry.moisture_alert);
  TEST_ASSERT_EQUAL_INT(0, msg.telemetry.water_level);  // only exactly 1 means low
}

void test_parse_rejects_junk() {
  InboundMessage msg;
  TEST_ASSERT_FALSE(parse("not json", msg));
  TEST_ASSERT_FALSE(parse("{\"type\":\"telemetry\"}", msg));  // moisture is mandatory
  TEST_ASSERT_FALSE(parse("{\"type\":\"hello\"}", msg));
}

void test_parse_ack() {
  InboundMessage ok;
  TEST_ASSERT_TRUE(parse("{\"type\":\"ack\",\"ok\":true}", ok));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MessageKind::Ack), static_cast<int>(ok.kind));
  TEST_ASSERT_TRUE(ok.ack_ok);

  InboundMessage bad;
  TEST_ASSERT_TRUE(parse("{\"type\":\"ack\",\"ok\":false}", bad));
  TEST_ASSERT_FALSE(bad.ack_ok);
}

void test_build_commands() {
  char buf[cfg::kMaxLineLen];
  TEST_ASSERT_TRUE(buildPumpCommand(buf, sizeof(buf), true) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"command\",\"pump\":1}\n", buf);
  TEST_ASSERT_TRUE(buildPumpCommand(buf, sizeof(buf), false) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"command\",\"pump\":2}\n", buf);

  TEST_ASSERT_TRUE(buildTemperatureCommand(buf, sizeof(buf), 23) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"command\",\"temperature\":{\"changed\":1,\"new_temp\":23}}\n", buf);
}

void test_build_command_refuses_bad_input() {
  char buf[cfg::kMaxLineLen];
  TEST_ASSERT_EQUAL_size_t(0, buildTemperatureCommand(buf, sizeof(buf), 14));
  TEST_ASSERT_EQUAL_size_t(0, buildTemperatureCommand(buf, sizeof(buf), 31));
  char tiny[8];
  TEST_ASSERT_EQUAL_size_t(0, buildPumpCommand(tiny, sizeof(tiny), true));
}

// --- state machine ------------------------------------------------------
static Telemetry frame(int raw, int water, int pump) {
  Telemetry t;
  t.moisture_raw = raw;
  t.water_level = water;
  t.pump = pump;
  t.has_pump = true;
  return t;
}

static Telemetry frameWithSetpoint(float setpoint_c) {
  Telemetry t = frame(2000, 0, 0);
  t.setpoint_c = setpoint_c;
  t.has_setpoint = true;
  return t;
}

void test_link_goes_down_after_timeout() {
  AppState s;
  TEST_ASSERT_FALSE(s.linkUp());
  s.onTelemetry(frame(2000, 0, 0), 1000);
  TEST_ASSERT_TRUE(s.linkUp());
  TEST_ASSERT_TRUE(s.consumeLinkChanged());
  TEST_ASSERT_FALSE(s.consumeLinkChanged());  // read once

  s.tick(1000 + cfg::kLinkTimeoutMs - 1);
  TEST_ASSERT_TRUE(s.linkUp());
  s.tick(1000 + cfg::kLinkTimeoutMs);
  TEST_ASSERT_FALSE(s.linkUp());
  TEST_ASSERT_TRUE(s.consumeLinkChanged());
}

void test_temperature_commits_only_on_positive_ack() {
  AppState s;
  const int before = s.targetTemperatureC();
  TEST_ASSERT_TRUE(s.requestTemperature(25, 1000));
  TEST_ASSERT_EQUAL_INT(before, s.targetTemperatureC());  // not yet
  TEST_ASSERT_FALSE(s.requestTemperature(26, 1010));      // one in flight at a time
  s.onAck(true, 1100);
  TEST_ASSERT_EQUAL_INT(25, s.targetTemperatureC());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandResult::Accepted),
                        static_cast<int>(s.consumeCommandResult()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandResult::None),
                        static_cast<int>(s.consumeCommandResult()));
}

void test_rejected_and_timed_out_commands_do_not_commit() {
  AppState rejected;
  TEST_ASSERT_TRUE(rejected.requestTemperature(28, 1000));
  rejected.onAck(false, 1100);
  TEST_ASSERT_EQUAL_INT(cfg::kTempDefaultC, rejected.targetTemperatureC());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandResult::Rejected),
                        static_cast<int>(rejected.consumeCommandResult()));

  AppState stalled;
  TEST_ASSERT_TRUE(stalled.requestTemperature(28, 1000));
  stalled.tick(1000 + cfg::kAckTimeoutMs - 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PendingCommand::Temperature),
                        static_cast<int>(stalled.pending()));
  stalled.tick(1000 + cfg::kAckTimeoutMs);
  TEST_ASSERT_EQUAL_INT(cfg::kTempDefaultC, stalled.targetTemperatureC());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandResult::TimedOut),
                        static_cast<int>(stalled.consumeCommandResult()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PendingCommand::None),
                        static_cast<int>(stalled.pending()));
}

void test_setpoint_out_of_range_is_refused() {
  AppState s;
  TEST_ASSERT_FALSE(s.requestTemperature(cfg::kTempMinC - 1, 1000));
  TEST_ASSERT_FALSE(s.requestTemperature(cfg::kTempMaxC + 1, 1000));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PendingCommand::None),
                        static_cast<int>(s.pending()));
}

void test_pump_follows_telemetry_except_while_in_flight() {
  AppState s;
  s.onTelemetry(frame(2000, 0, 1), 1000);
  TEST_ASSERT_TRUE(s.pumpOn());

  TEST_ASSERT_TRUE(s.requestPump(false, 1100));
  s.onTelemetry(frame(2000, 0, 1), 1200);  // relay has not switched yet
  TEST_ASSERT_TRUE(s.pumpOn());            // stays put, no flicker
  s.onAck(true, 1250);
  TEST_ASSERT_FALSE(s.pumpOn());

  s.onTelemetry(frame(2000, 0, 1), 1300);  // controller still reports ON
  TEST_ASSERT_TRUE(s.pumpOn());            // actual state wins
}

void test_setpoint_echo_adopts_controller_value() {
  AppState s;
  TEST_ASSERT_FALSE(s.setpointKnown());
  TEST_ASSERT_EQUAL_INT(cfg::kTempDefaultC, s.targetTemperatureC());

  s.onTelemetry(frameWithSetpoint(26.0f), 1000);
  TEST_ASSERT_TRUE(s.setpointKnown());
  TEST_ASSERT_EQUAL_INT(26, s.targetTemperatureC());

  s.onTelemetry(frameWithSetpoint(23.4f), 2000);  // rounded, and clamped to range
  TEST_ASSERT_EQUAL_INT(23, s.targetTemperatureC());
  s.onTelemetry(frameWithSetpoint(99.0f), 3000);
  TEST_ASSERT_EQUAL_INT(cfg::kTempMaxC, s.targetTemperatureC());
}

void test_setpoint_echo_does_not_fight_a_command() {
  AppState s;
  s.onTelemetry(frameWithSetpoint(20.0f), 1000);
  TEST_ASSERT_EQUAL_INT(20, s.targetTemperatureC());

  TEST_ASSERT_TRUE(s.requestTemperature(27, 1100));
  s.onTelemetry(frameWithSetpoint(20.0f), 1200);   // still the old setpoint
  TEST_ASSERT_EQUAL_INT(20, s.targetTemperatureC());
  s.onAck(true, 1250);
  TEST_ASSERT_EQUAL_INT(27, s.targetTemperatureC());

  // Controller acked before applying: the stale echo must not bounce the number.
  s.onTelemetry(frameWithSetpoint(20.0f), 1300);
  TEST_ASSERT_EQUAL_INT(27, s.targetTemperatureC());
  s.onTelemetry(frameWithSetpoint(27.0f), 1400);
  TEST_ASSERT_EQUAL_INT(27, s.targetTemperatureC());

  // Once the grace window is over, the controller is authoritative again: an
  // operator turning the physical knob on the box wins.
  s.onTelemetry(frameWithSetpoint(19.0f), 1250 + cfg::kSetpointEchoGraceMs);
  TEST_ASSERT_EQUAL_INT(19, s.targetTemperatureC());
}

// --- bring-up test button -----------------------------------------------
static Telemetry frameWithTest(int button, int count) {
  Telemetry t = frame(2000, 0, 0);
  t.test_button = button;
  t.has_test_button = true;
  t.test_count = count;
  t.has_test_count = true;
  return t;
}

void test_test_indicator_is_absent_until_the_controller_sends_it() {
  AppState s;
  s.onTelemetry(frame(2000, 0, 0), 1000);
  TEST_ASSERT_FALSE(s.testActive());  // stays off screen in production
  s.onTelemetry(frameWithTest(0, 0), 2000);
  TEST_ASSERT_TRUE(s.testActive());
}

void test_test_counter_adopts_baseline_without_a_false_press() {
  AppState s;
  s.onTelemetry(frameWithTest(0, 7), 1000);  // he pressed 7 times before we booted
  TEST_ASSERT_EQUAL_INT(7, s.testCount());
  TEST_ASSERT_EQUAL_INT(0, s.consumeTestPresses());
  TEST_ASSERT_FALSE(s.testLampOn(1000));
}

void test_test_counter_catches_a_press_missed_between_heartbeats() {
  AppState s;
  s.onTelemetry(frameWithTest(0, 0), 1000);
  // Button went down and back up between two frames: state is 0 both times,
  // only the counter shows it happened.
  s.onTelemetry(frameWithTest(0, 1), 2000);
  TEST_ASSERT_EQUAL_INT(1, s.consumeTestPresses());
  TEST_ASSERT_EQUAL_INT(1, s.testCount());
  TEST_ASSERT_TRUE(s.testLampOn(2000));
  TEST_ASSERT_TRUE(s.testLampOn(2000 + cfg::kTestLampHoldMs - 1));
  TEST_ASSERT_FALSE(s.testLampOn(2000 + cfg::kTestLampHoldMs));
}

void test_test_counter_reports_a_double_press_as_two() {
  AppState s;
  s.onTelemetry(frameWithTest(0, 4), 1000);
  s.onTelemetry(frameWithTest(0, 6), 2000);
  TEST_ASSERT_EQUAL_INT(2, s.consumeTestPresses());
  TEST_ASSERT_EQUAL_INT(6, s.testCount());
}

void test_test_lamp_follows_a_held_button() {
  AppState s;
  s.onTelemetry(frameWithTest(1, 1), 1000);
  TEST_ASSERT_TRUE(s.testLampOn(1000 + 5 * cfg::kTestLampHoldMs));  // still held
  s.onTelemetry(frameWithTest(0, 1), 9000);
  TEST_ASSERT_FALSE(s.testLampOn(9000));
}

void test_controller_reboot_resets_the_counter_without_counting_presses() {
  AppState s;
  s.onTelemetry(frameWithTest(0, 9), 1000);
  s.onTelemetry(frameWithTest(0, 0), 2000);
  TEST_ASSERT_EQUAL_INT(0, s.testCount());
  TEST_ASSERT_EQUAL_INT(0, s.consumeTestPresses());
  TEST_ASSERT_TRUE(s.consumeTestCounterReset());
  TEST_ASSERT_FALSE(s.consumeTestCounterReset());
}

void test_button_edge_counts_when_no_counter_is_available() {
  AppState s;
  Telemetry down = frame(2000, 0, 0);
  down.test_button = 1;
  down.has_test_button = true;
  Telemetry up = frame(2000, 0, 0);
  up.test_button = 0;
  up.has_test_button = true;

  s.onTelemetry(up, 1000);
  TEST_ASSERT_EQUAL_INT(0, s.consumeTestPresses());
  s.onTelemetry(down, 2000);
  TEST_ASSERT_EQUAL_INT(1, s.consumeTestPresses());
  s.onTelemetry(down, 3000);  // still held, not a new press
  TEST_ASSERT_EQUAL_INT(0, s.consumeTestPresses());
  s.onTelemetry(up, 4000);
  s.onTelemetry(down, 5000);
  TEST_ASSERT_EQUAL_INT(1, s.consumeTestPresses());
}

void test_low_water_alarm_edge_and_dismiss() {
  AppState s;
  s.onTelemetry(frame(2000, 1, 0), 1000);
  TEST_ASSERT_TRUE(s.alarmActive());
  TEST_ASSERT_TRUE(s.waterLow());

  TEST_ASSERT_FALSE(s.alarmDismissAllowed(1000 + cfg::kAlarmMinFlashMs - 1));
  TEST_ASSERT_FALSE(s.dismissAlarm(1000 + cfg::kAlarmMinFlashMs - 1));
  TEST_ASSERT_TRUE(s.alarmActive());

  TEST_ASSERT_TRUE(s.dismissAlarm(1000 + cfg::kAlarmMinFlashMs));
  TEST_ASSERT_FALSE(s.alarmActive());
  TEST_ASSERT_TRUE(s.waterLow());  // banner stays until the tank is refilled

  s.onTelemetry(frame(2000, 1, 0), 6000);  // heartbeat repeats water_level 1
  TEST_ASSERT_FALSE(s.alarmActive());      // must not re-open every second
}

void test_low_water_alarm_retriggers_after_refill() {
  AppState s;
  s.onTelemetry(frame(2000, 1, 0), 1000);
  s.dismissAlarm(1000 + cfg::kAlarmMinFlashMs);
  s.onTelemetry(frame(2000, 0, 0), 10000);
  TEST_ASSERT_FALSE(s.waterLow());
  s.onTelemetry(frame(2000, 1, 0), 20000);
  TEST_ASSERT_TRUE(s.alarmActive());
  TEST_ASSERT_EQUAL_UINT32(20000, s.alarmStartedMs());
}

// --- mock link ----------------------------------------------------------
void test_mock_link_emits_parseable_telemetry_and_acks() {
  MockLink mock;
  mock.begin(0);
  AppState s;
  int telemetry_frames = 0;
  int acks = 0;
  bool sent = false;

  char line[cfg::kMaxLineLen];
  size_t len = 0;
  for (uint32_t now = 0; now <= 3000; now += 10) {
    if (mock.poll(now, line, sizeof(line), len)) {
      TEST_ASSERT_EQUAL_CHAR('\n', line[len - 1]);
      InboundMessage msg;
      TEST_ASSERT_TRUE(parseMessage(line, len - 1, msg));
      if (msg.kind == MessageKind::Telemetry) {
        telemetry_frames++;
        s.onTelemetry(msg.telemetry, now);
      } else if (msg.kind == MessageKind::Ack) {
        acks++;
        s.onAck(msg.ack_ok, now);
      }
    }
    if (!sent && now == 1500) {
      sent = true;
      s.requestPump(true, now);
      char cmd[cfg::kMaxLineLen];
      const size_t n = buildPumpCommand(cmd, sizeof(cmd), true);
      mock.onCommand(cmd, n, now);
    }
    s.tick(now);
  }

  TEST_ASSERT_TRUE(telemetry_frames >= 3);  // ~1 Hz heartbeat
  TEST_ASSERT_EQUAL_INT(1, acks);
  TEST_ASSERT_TRUE(s.linkUp());
  TEST_ASSERT_TRUE(s.pumpOn());
  TEST_ASSERT_TRUE(s.setpointKnown());  // adopted from the mock controller
  TEST_ASSERT_EQUAL_INT(24, s.targetTemperatureC());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_moisture_percent_band);
  RUN_TEST(test_moisture_percent_clamps);
  RUN_TEST(test_line_assembler_lf_and_crlf);
  RUN_TEST(test_line_assembler_skips_blank_lines);
  RUN_TEST(test_line_assembler_drops_overlong_line);
  RUN_TEST(test_parse_full_telemetry);
  RUN_TEST(test_parse_marks_missing_optional_fields);
  RUN_TEST(test_parse_setpoint);
  RUN_TEST(test_parse_test_button_fields);
  RUN_TEST(test_parse_accepts_legacy_humidity_names);
  RUN_TEST(test_parse_clamps_out_of_range_values);
  RUN_TEST(test_parse_rejects_junk);
  RUN_TEST(test_parse_ack);
  RUN_TEST(test_build_commands);
  RUN_TEST(test_build_command_refuses_bad_input);
  RUN_TEST(test_link_goes_down_after_timeout);
  RUN_TEST(test_temperature_commits_only_on_positive_ack);
  RUN_TEST(test_rejected_and_timed_out_commands_do_not_commit);
  RUN_TEST(test_setpoint_out_of_range_is_refused);
  RUN_TEST(test_pump_follows_telemetry_except_while_in_flight);
  RUN_TEST(test_setpoint_echo_adopts_controller_value);
  RUN_TEST(test_setpoint_echo_does_not_fight_a_command);
  RUN_TEST(test_test_indicator_is_absent_until_the_controller_sends_it);
  RUN_TEST(test_test_counter_adopts_baseline_without_a_false_press);
  RUN_TEST(test_test_counter_catches_a_press_missed_between_heartbeats);
  RUN_TEST(test_test_counter_reports_a_double_press_as_two);
  RUN_TEST(test_test_lamp_follows_a_held_button);
  RUN_TEST(test_controller_reboot_resets_the_counter_without_counting_presses);
  RUN_TEST(test_button_edge_counts_when_no_counter_is_available);
  RUN_TEST(test_low_water_alarm_edge_and_dismiss);
  RUN_TEST(test_low_water_alarm_retriggers_after_refill);
  RUN_TEST(test_mock_link_emits_parseable_telemetry_and_acks);
  return UNITY_END();
}
