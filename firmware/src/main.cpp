// Auto Hydro HMI v1 — CrowPanel 7" ESP32-S3 panel talking NDJSON to Parker's
// controller over UART0. Protocol: docs/INTEGRATION_GUIDE_en.md v1.2.
//
// UART0 is the Parker link AND the USB serial port on this board. Nothing may
// print to it: every debug message goes to the on-screen log instead.
#include <Arduino.h>
#include <lvgl.h>

#include "app_config.h"
#include "app_state.h"
#include "display_driver.h"
#include "line_assembler.h"
#include "protocol.h"
#include "types.h"
#include "ui.h"

#if AUTOHYDRO_MOCK
#include "mock_link.h"
#endif

namespace {

AppState g_state;
LineAssembler<cfg::kMaxLineLen> g_rx;
uint32_t g_next_render_ms = 0;

#if AUTOHYDRO_MOCK
MockLink g_mock;
#endif

void linkWrite(const char* data, size_t len) {
#if AUTOHYDRO_MOCK
  g_mock.onCommand(data, len, millis());
#else
  Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#endif
}

void handleLine(const char* line, size_t len, uint32_t now) {
  InboundMessage msg;
  if (!parseMessage(line, len, msg)) {
    ui::log("bad frame dropped");
    return;
  }
  switch (msg.kind) {
    case MessageKind::Telemetry:
      g_state.onTelemetry(msg.telemetry, now);
      break;
    case MessageKind::Ack:
      g_state.onAck(msg.ack_ok, now);
      break;
    default:
      break;
  }
}

void sendPump(bool on) {
  const uint32_t now = millis();
  if (!g_state.requestPump(on, now)) {
    ui::log("busy: previous command not acked");
    return;
  }
  char buf[cfg::kMaxLineLen];
  const size_t n = buildPumpCommand(buf, sizeof(buf), on);
  if (n == 0) return;
  linkWrite(buf, n);
  ui::log("TX pump %s", on ? "ON" : "OFF");
}

void sendTemperature(int celsius) {
  const uint32_t now = millis();
  if (!g_state.requestTemperature(celsius, now)) {
    ui::log("busy or out of range: %d C", celsius);
    return;
  }
  char buf[cfg::kMaxLineLen];
  const size_t n = buildTemperatureCommand(buf, sizeof(buf), celsius);
  if (n == 0) return;
  linkWrite(buf, n);
  ui::log("TX target temp %d C", celsius);
}

void dismissAlarm() {
  if (g_state.dismissAlarm(millis())) {
    ui::log("low water acknowledged");
  }
}

void pumpLink(uint32_t now) {
#if AUTOHYDRO_MOCK
  // Fed through the same assembler as the real link, so mock mode exercises
  // line splitting and JSON parsing too.
  char line[cfg::kMaxLineLen];
  size_t len = 0;
  if (g_mock.poll(now, line, sizeof(line), len)) {
    for (size_t i = 0; i < len; ++i) {
      if (g_rx.push(line[i])) {
        handleLine(g_rx.line(), g_rx.line_len(), now);
      }
    }
  }
#else
  // Bounded per-loop read so LVGL still gets serviced under a chatty link.
  for (int i = 0; i < 512 && Serial.available() > 0; ++i) {
    if (g_rx.push(static_cast<char>(Serial.read()))) {
      handleLine(g_rx.line(), g_rx.line_len(), now);
    }
  }
#endif
}

void reportCommandResult() {
  switch (g_state.consumeCommandResult()) {
    case CommandResult::Accepted:
      ui::log("ACK ok");
      break;
    case CommandResult::Rejected:
      ui::log("ACK rejected, value reverted");
      break;
    case CommandResult::TimedOut:
      ui::log("no ACK in %u ms, value reverted",
              static_cast<unsigned>(cfg::kAckTimeoutMs));
      break;
    default:
      break;
  }
  if (g_state.consumeLinkChanged()) {
    ui::log(g_state.linkUp() ? "link up" : "LINK LOST");
  }

  // The log gives a countable history, so a press that the eye missed is still
  // provable after the fact.
  const int presses = g_state.consumeTestPresses();
  if (presses > 0) {
    ui::log("TEST BUTTON +%d, count %d", presses, g_state.testCount());
  }
  if (g_state.consumeTestCounterReset()) {
    ui::log("test counter reset to %d (controller rebooted?)", g_state.testCount());
  }
}

}  // namespace

void setup() {
  Serial.begin(cfg::kUartBaud, SERIAL_8N1, cfg::kUartRxPin, cfg::kUartTxPin);

  if (!hmiDisplayBegin()) {
    // Nothing to report to: the link is the only serial port. Halt visibly dark.
    while (true) delay(1000);
  }

  ui::Callbacks cb;
  cb.sendPump = sendPump;
  cb.sendTemperature = sendTemperature;
  cb.dismissAlarm = dismissAlarm;
  ui::init(cb, g_state);

  lv_timer_handler();
  hmiSetBrightness(200);

#if AUTOHYDRO_MOCK
  g_mock.begin(millis());
  ui::log("MOCK MODE: telemetry is synthetic");
#endif
}

void loop() {
  const uint32_t now = millis();

  pumpLink(now);
  g_state.tick(now);
  reportCommandResult();

  if (static_cast<int32_t>(now - g_next_render_ms) >= 0) {
    g_next_render_ms = now + cfg::kUiRefreshMs;
    ui::render(g_state, now);
  }

  lv_timer_handler();
  delay(5);
}
