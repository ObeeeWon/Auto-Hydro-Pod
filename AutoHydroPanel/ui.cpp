// Auto Hydro HMI screen — 800x480, high-contrast industrial panel, no animation.
// Labels are English on purpose: a Chinese subset font still has to be generated
// with lv_font_conv and 4 MB of flash is tight. See notes at the end of the file.
#include "ui.h"

#include <lvgl.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "app_state.h"
#include "moisture.h"

namespace ui {
namespace {

// --- palette ------------------------------------------------------------
constexpr uint32_t kColBg = 0x0E1114;
constexpr uint32_t kColCard = 0x1B2126;
constexpr uint32_t kColEdge = 0x39434B;
constexpr uint32_t kColText = 0xC9D3D9;
constexpr uint32_t kColDim = 0x7C878E;
constexpr uint32_t kColValue = 0xE8C64A;  // amber digits, like an old gauge
constexpr uint32_t kColOk = 0x4FBF6A;
constexpr uint32_t kColWarn = 0xE08A2B;
constexpr uint32_t kColAlarm = 0xC93B2B;
constexpr uint32_t kColBtn = 0x2C353C;

Callbacks g_cb{};

lv_obj_t* g_link_label = nullptr;
lv_obj_t* g_test_chip = nullptr;
lv_obj_t* g_test_lamp = nullptr;
lv_obj_t* g_test_label = nullptr;
lv_obj_t* g_moisture_pct = nullptr;
lv_obj_t* g_moisture_bar = nullptr;
lv_obj_t* g_moisture_status = nullptr;
lv_obj_t* g_moisture_raw = nullptr;
lv_obj_t* g_temp_target = nullptr;
lv_obj_t* g_temp_slider = nullptr;
lv_obj_t* g_temp_measured = nullptr;
lv_obj_t* g_temp_hint = nullptr;
lv_obj_t* g_pump_switch = nullptr;
lv_obj_t* g_pump_state = nullptr;
lv_obj_t* g_water_panel = nullptr;
lv_obj_t* g_water_state = nullptr;
lv_obj_t* g_log_label = nullptr;

lv_obj_t* g_dialog = nullptr;  // scrim; box is its only child
lv_obj_t* g_dialog_title = nullptr;
lv_obj_t* g_dialog_text = nullptr;

lv_obj_t* g_alarm = nullptr;
lv_obj_t* g_alarm_title = nullptr;
lv_obj_t* g_alarm_button = nullptr;
lv_obj_t* g_alarm_button_label = nullptr;
lv_timer_t* g_flash_timer = nullptr;
bool g_flash_bright = true;

enum class Dialog : uint8_t { None, Pump, Temperature };
Dialog g_dialog_kind = Dialog::None;
bool g_dialog_pump_on = false;
int g_dialog_temp_c = cfg::kTempDefaultC;

// Guards render()'s programmatic widget writes from firing user callbacks.
bool g_syncing = false;

constexpr int kLogLines = 3;
constexpr size_t kLogWidth = 72;
char g_log[kLogLines][kLogWidth] = {};

// --- small style helpers ------------------------------------------------
void styleFlat(lv_obj_t* obj, uint32_t bg, uint32_t border) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
  lv_obj_set_style_border_width(obj, 2, 0);
  lv_obj_set_style_radius(obj, 2, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_anim_duration(obj, 0, 0);
  lv_obj_set_scrollable(obj, false);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* makeCard(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  styleFlat(card, kColCard, kColEdge);
  return card;
}

lv_obj_t* makeLabel(lv_obj_t* parent, int32_t x, int32_t y, const char* text,
                    const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

lv_obj_t* makeButton(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char* text, const lv_font_t* font, lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  styleFlat(btn, kColBtn, kColEdge);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x121a1f), LV_STATE_PRESSED);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
  lv_obj_center(label);
  if (cb != nullptr) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  return btn;
}

// --- dialog -------------------------------------------------------------
void closeDialog() {
  g_dialog_kind = Dialog::None;
  lv_obj_set_hidden(g_dialog, true);
}

void onDialogConfirm(lv_event_t* e) {
  (void)e;
  const Dialog kind = g_dialog_kind;
  closeDialog();
  if (kind == Dialog::Pump) {
    if (g_cb.sendPump != nullptr) g_cb.sendPump(g_dialog_pump_on);
  } else if (kind == Dialog::Temperature) {
    if (g_cb.sendTemperature != nullptr) g_cb.sendTemperature(g_dialog_temp_c);
  }
}

void onDialogCancel(lv_event_t* e) {
  (void)e;
  closeDialog();  // render() resyncs the widget on the next pass
  log("cancelled, nothing sent");
}

void openDialog(Dialog kind, const char* title, const char* text) {
  g_dialog_kind = kind;
  lv_label_set_text(g_dialog_title, title);
  lv_label_set_text(g_dialog_text, text);
  lv_obj_set_hidden(g_dialog, false);
}

// --- control events -----------------------------------------------------
void onPumpToggled(lv_event_t* e) {
  if (g_syncing) return;
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  g_dialog_pump_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  openDialog(Dialog::Pump, "CONFIRM PUMP",
             g_dialog_pump_on ? "Set pump to ON?" : "Set pump to OFF?");
}

void onSliderChanged(lv_event_t* e) {
  if (g_syncing) return;
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const int value = static_cast<int>(lv_slider_get_value(slider));
  lv_label_set_text_fmt(g_temp_target, "%d °C", value);  // preview only
}

void onSliderReleased(lv_event_t* e) {
  if (g_syncing) return;
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  g_dialog_temp_c = static_cast<int>(lv_slider_get_value(slider));
  char msg[48];
  std::snprintf(msg, sizeof(msg), "Set target temperature to %d °C?", g_dialog_temp_c);
  openDialog(Dialog::Temperature, "CONFIRM TEMPERATURE", msg);
}

void onAlarmAck(lv_event_t* e) {
  (void)e;
  if (g_cb.dismissAlarm != nullptr) g_cb.dismissAlarm();
}

void flashTimerCb(lv_timer_t* t) {
  (void)t;
  g_flash_bright = !g_flash_bright;
  lv_obj_set_style_bg_color(g_alarm, lv_color_hex(g_flash_bright ? kColAlarm : 0x140A08), 0);
  lv_obj_set_style_text_color(g_alarm_title,
                              lv_color_hex(g_flash_bright ? 0xFFFFFF : kColAlarm), 0);
}

// --- build --------------------------------------------------------------
void buildStatusBar(lv_obj_t* parent) {
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, 800, 40);
  styleFlat(bar, 0x151B20, kColEdge);
  lv_obj_set_style_border_width(bar, 0, 0);

  // Product name. No date or clock: the panel is offline by design.
  makeLabel(bar, 16, 6, "AUTO HYDRO", &lv_font_montserrat_28, kColText);
  g_link_label = makeLabel(bar, 0, 10, "NO LINK", &lv_font_montserrat_20, kColDim);
  lv_obj_align(g_link_label, LV_ALIGN_RIGHT_MID, -16, 0);

  // Bring-up chip for Parker's physical test button. Hidden until his telemetry
  // carries a test field, so nothing has to be stripped for production.
  g_test_chip = lv_obj_create(bar);
  lv_obj_set_pos(g_test_chip, 300, 4);
  lv_obj_set_size(g_test_chip, 210, 32);
  styleFlat(g_test_chip, 0x0B0F12, kColEdge);
  lv_obj_set_hidden(g_test_chip, true);

  g_test_lamp = lv_obj_create(g_test_chip);
  lv_obj_set_pos(g_test_lamp, 8, 5);
  lv_obj_set_size(g_test_lamp, 20, 20);
  styleFlat(g_test_lamp, 0x2A3238, 0x2A3238);
  lv_obj_set_style_radius(g_test_lamp, LV_RADIUS_CIRCLE, 0);

  g_test_label = makeLabel(g_test_chip, 38, 3, "TEST  0", &lv_font_montserrat_20, kColDim);
}

void buildMoistureCard(lv_obj_t* parent) {
  lv_obj_t* card = makeCard(parent, 8, 48, 388, 206);
  makeLabel(card, 14, 8, "MOISTURE", &lv_font_montserrat_20, kColDim);

  g_moisture_pct = makeLabel(card, 14, 30, "-- %", &lv_font_montserrat_48, kColValue);

  g_moisture_bar = lv_bar_create(card);
  lv_obj_set_pos(g_moisture_bar, 14, 96);
  lv_obj_set_size(g_moisture_bar, 360, 24);
  lv_bar_set_range(g_moisture_bar, 0, 100);
  lv_bar_set_value(g_moisture_bar, 0, LV_ANIM_OFF);
  styleFlat(g_moisture_bar, 0x0B0F12, kColEdge);
  lv_obj_set_style_bg_color(g_moisture_bar, lv_color_hex(kColOk), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_moisture_bar, 0, LV_PART_INDICATOR);

  // Tick marks on the bar itself: band edges at 45/60 %, control target at 55 %.
  const int ticks[] = {cfg::kMoisturePctLow, cfg::kMoisturePctTarget, cfg::kMoisturePctHigh};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* tick = lv_obj_create(card);
    lv_obj_set_size(tick, 2, 24);
    lv_obj_set_pos(tick, 14 + (360 * ticks[i]) / 100, 96);
    const bool is_target = (i == 1);
    styleFlat(tick, is_target ? kColValue : kColText, is_target ? kColValue : kColText);
    lv_obj_set_style_border_width(tick, 0, 0);
  }
  makeLabel(card, 14, 126, "BAND 45-60 %    TARGET 55 %", &lv_font_montserrat_14, kColDim);

  g_moisture_status = makeLabel(card, 14, 150, "STATUS  --", &lv_font_montserrat_20, kColDim);
  g_moisture_raw = makeLabel(card, 14, 178, "RAW  ----", &lv_font_montserrat_14, kColDim);
}

void buildTemperatureCard(lv_obj_t* parent) {
  lv_obj_t* card = makeCard(parent, 404, 48, 388, 206);
  makeLabel(card, 14, 8, "TARGET TEMP", &lv_font_montserrat_20, kColDim);

  g_temp_target = makeLabel(card, 14, 30, "-- °C", &lv_font_montserrat_48, kColValue);

  g_temp_slider = lv_slider_create(card);
  lv_obj_set_pos(g_temp_slider, 14, 100);
  lv_obj_set_size(g_temp_slider, 360, 20);
  lv_slider_set_range(g_temp_slider, cfg::kTempMinC, cfg::kTempMaxC);
  lv_slider_set_value(g_temp_slider, cfg::kTempDefaultC, LV_ANIM_OFF);
  styleFlat(g_temp_slider, 0x0B0F12, kColEdge);
  lv_obj_set_style_bg_color(g_temp_slider, lv_color_hex(kColBtn), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_temp_slider, lv_color_hex(kColValue), LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_temp_slider, 14, LV_PART_KNOB);  // fat knob for fingers
  lv_obj_set_ext_click_area(g_temp_slider, 24);               // >= 60 px touch target
  lv_obj_add_event_cb(g_temp_slider, onSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(g_temp_slider, onSliderReleased, LV_EVENT_RELEASED, nullptr);

  makeLabel(card, 14, 128, "15", &lv_font_montserrat_14, kColDim);
  lv_obj_t* max_label = makeLabel(card, 0, 128, "30", &lv_font_montserrat_14, kColDim);
  lv_obj_set_pos(max_label, 360, 128);

  g_temp_measured = makeLabel(card, 14, 150, "MEASURED  -- °C", &lv_font_montserrat_20, kColDim);
  g_temp_hint = makeLabel(card, 14, 178, "Release finger to confirm", &lv_font_montserrat_14, kColDim);
}

void buildBottomCard(lv_obj_t* parent) {
  lv_obj_t* card = makeCard(parent, 8, 262, 784, 134);

  lv_obj_t* pump_panel = lv_obj_create(card);
  lv_obj_set_pos(pump_panel, 0, 0);
  lv_obj_set_size(pump_panel, 392, 130);
  styleFlat(pump_panel, kColCard, kColCard);
  makeLabel(pump_panel, 14, 8, "PUMP", &lv_font_montserrat_20, kColDim);

  g_pump_switch = lv_switch_create(pump_panel);
  lv_obj_set_pos(g_pump_switch, 14, 44);
  lv_obj_set_size(g_pump_switch, 130, 64);
  styleFlat(g_pump_switch, 0x0B0F12, kColEdge);
  lv_obj_set_style_bg_color(g_pump_switch, lv_color_hex(kColOk), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_pump_switch, lv_color_hex(kColText), LV_PART_KNOB);
  lv_obj_add_event_cb(g_pump_switch, onPumpToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  g_pump_state = makeLabel(pump_panel, 164, 60, "OFF", &lv_font_montserrat_28, kColText);

  g_water_panel = lv_obj_create(card);
  lv_obj_set_pos(g_water_panel, 396, 0);
  lv_obj_set_size(g_water_panel, 384, 130);
  styleFlat(g_water_panel, kColCard, kColEdge);
  makeLabel(g_water_panel, 14, 8, "WATER", &lv_font_montserrat_20, kColDim);
  g_water_state = makeLabel(g_water_panel, 14, 56, "--", &lv_font_montserrat_28, kColText);
}

void buildLogPanel(lv_obj_t* parent) {
  lv_obj_t* panel = makeCard(parent, 8, 404, 784, 68);
  g_log_label = makeLabel(panel, 12, 6, "", &lv_font_montserrat_14, kColDim);
  lv_obj_set_width(g_log_label, 760);
}

void buildDialog() {
  g_dialog = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(g_dialog, 0, 0);
  lv_obj_set_size(g_dialog, 800, 480);
  lv_obj_set_style_bg_color(g_dialog, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_dialog, LV_OPA_70, 0);
  lv_obj_set_style_border_width(g_dialog, 0, 0);
  lv_obj_set_style_radius(g_dialog, 0, 0);
  lv_obj_set_scrollable(g_dialog, false);
  lv_obj_set_hidden(g_dialog, true);

  lv_obj_t* box = lv_obj_create(g_dialog);
  lv_obj_set_size(box, 560, 250);
  lv_obj_center(box);
  styleFlat(box, kColCard, kColEdge);

  g_dialog_title = makeLabel(box, 24, 20, "", &lv_font_montserrat_28, kColText);
  g_dialog_text = makeLabel(box, 24, 74, "", &lv_font_montserrat_20, kColText);
  lv_obj_set_width(g_dialog_text, 510);

  makeButton(box, 24, 154, 240, 76, "CANCEL", &lv_font_montserrat_20, onDialogCancel);
  lv_obj_t* ok = makeButton(box, 292, 154, 240, 76, "CONFIRM", &lv_font_montserrat_20,
                            onDialogConfirm);
  lv_obj_set_style_border_color(ok, lv_color_hex(kColOk), 0);
}

void buildAlarm() {
  // System layer so the alarm always covers the confirm dialog.
  g_alarm = lv_obj_create(lv_layer_sys());
  lv_obj_set_pos(g_alarm, 0, 0);
  lv_obj_set_size(g_alarm, 800, 480);
  styleFlat(g_alarm, kColAlarm, kColAlarm);
  lv_obj_set_hidden(g_alarm, true);

  g_alarm_title = makeLabel(g_alarm, 0, 110, "LOW WATER", &lv_font_montserrat_48, 0xFFFFFF);
  lv_obj_align(g_alarm_title, LV_ALIGN_TOP_MID, 0, 110);

  lv_obj_t* sub = makeLabel(g_alarm, 0, 190, "CHECK RESERVOIR", &lv_font_montserrat_28, 0xFFFFFF);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 190);

  g_alarm_button = makeButton(g_alarm, 0, 0, 420, 120, "ACKNOWLEDGE",
                              &lv_font_montserrat_28, onAlarmAck);
  lv_obj_align(g_alarm_button, LV_ALIGN_TOP_MID, 0, 290);
  g_alarm_button_label = lv_obj_get_child(g_alarm_button, 0);

  g_flash_timer = lv_timer_create(flashTimerCb, cfg::kAlarmFlashPeriodMs, nullptr);
  lv_timer_pause(g_flash_timer);
}

}  // namespace

void init(const Callbacks& cb, const AppState& state) {
  g_cb = cb;

  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kColBg), 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_scrollable(screen, false);

  buildStatusBar(screen);
  buildMoistureCard(screen);
  buildTemperatureCard(screen);
  buildBottomCard(screen);
  buildLogPanel(screen);
  buildDialog();
  buildAlarm();

  render(state, 0);
  log("AUTO HYDRO ready, waiting for telemetry");
}

void render(const AppState& state, uint32_t now_ms) {
  g_syncing = true;

  const bool live = state.linkUp() && state.hasTelemetry();
  const Telemetry& t = state.telemetry();

  // --- link ---
  if (live) {
    lv_label_set_text(g_link_label, "LINK OK");
    lv_obj_set_style_text_color(g_link_label, lv_color_hex(kColOk), 0);
  } else if (state.hasTelemetry()) {
    lv_label_set_text(g_link_label, "COMM FAULT");
    lv_obj_set_style_text_color(g_link_label, lv_color_hex(kColAlarm), 0);
  } else {
    lv_label_set_text(g_link_label, "NO LINK");
    lv_obj_set_style_text_color(g_link_label, lv_color_hex(kColDim), 0);
  }

  // --- bring-up test button ---
  if (state.testActive()) {
    lv_obj_set_hidden(g_test_chip, false);
    lv_label_set_text_fmt(g_test_label, "TEST  %d", state.testCount());
    const bool lamp = live && state.testLampOn(now_ms);
    // Whole chip inverts on a press so it reads from across the bench.
    lv_obj_set_style_bg_color(g_test_chip, lv_color_hex(lamp ? kColOk : 0x0B0F12), 0);
    lv_obj_set_style_border_color(g_test_chip, lv_color_hex(lamp ? 0xFFFFFF : kColEdge), 0);
    lv_obj_set_style_bg_color(g_test_lamp, lv_color_hex(lamp ? 0xFFFFFF : 0x2A3238), 0);
    lv_obj_set_style_text_color(g_test_label,
                                lv_color_hex(lamp ? 0x0E1114 : (live ? kColText : kColDim)), 0);
  }

  // --- moisture: never show a stale number once the link drops ---
  if (live) {
    const int pct = moisture_to_percent(t.moisture_raw);
    lv_label_set_text_fmt(g_moisture_pct, "%d %%", pct);
    lv_obj_set_style_text_color(g_moisture_pct, lv_color_hex(kColValue), 0);
    lv_bar_set_value(g_moisture_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_moisture_raw, "RAW  %d", t.moisture_raw);

    switch (static_cast<MoistureAlert>(t.moisture_alert)) {
      case MoistureAlert::TooWet:
        lv_label_set_text(g_moisture_status, "STATUS  TOO WET");
        lv_obj_set_style_text_color(g_moisture_status, lv_color_hex(kColWarn), 0);
        lv_obj_set_style_bg_color(g_moisture_bar, lv_color_hex(kColWarn), LV_PART_INDICATOR);
        break;
      case MoistureAlert::TooDry:
        lv_label_set_text(g_moisture_status, "STATUS  TOO DRY");
        lv_obj_set_style_text_color(g_moisture_status, lv_color_hex(kColAlarm), 0);
        lv_obj_set_style_bg_color(g_moisture_bar, lv_color_hex(kColAlarm), LV_PART_INDICATOR);
        break;
      default:
        lv_label_set_text(g_moisture_status, "STATUS  OK");
        lv_obj_set_style_text_color(g_moisture_status, lv_color_hex(kColOk), 0);
        lv_obj_set_style_bg_color(g_moisture_bar, lv_color_hex(kColOk), LV_PART_INDICATOR);
        break;
    }
  } else {
    lv_label_set_text(g_moisture_pct, "-- %");
    lv_obj_set_style_text_color(g_moisture_pct, lv_color_hex(kColDim), 0);
    lv_bar_set_value(g_moisture_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(g_moisture_status, "STATUS  --");
    lv_obj_set_style_text_color(g_moisture_status, lv_color_hex(kColDim), 0);
    lv_label_set_text(g_moisture_raw, "RAW  ----");
  }

  // --- temperature ---
  const bool temp_busy = (g_dialog_kind == Dialog::Temperature) ||
                         (state.pending() == PendingCommand::Temperature);
  if (!temp_busy) {
    lv_label_set_text_fmt(g_temp_target, "%d °C", state.targetTemperatureC());
    lv_slider_set_value(g_temp_slider, state.targetTemperatureC(), LV_ANIM_OFF);
  }
  if (live && t.has_temperature) {
    lv_label_set_text_fmt(g_temp_measured, "MEASURED  %.1f °C", static_cast<double>(t.temperature_c));
    lv_obj_set_style_text_color(g_temp_measured, lv_color_hex(kColText), 0);
  } else {
    lv_label_set_text(g_temp_measured, "MEASURED  -- °C");
    lv_obj_set_style_text_color(g_temp_measured, lv_color_hex(kColDim), 0);
  }
  if (state.pending() == PendingCommand::Temperature) {
    lv_label_set_text(g_temp_hint, "WAITING FOR ACK...");
    lv_obj_set_style_text_color(g_temp_hint, lv_color_hex(kColDim), 0);
  } else if (!state.setpointKnown()) {
    // Be explicit that this number is our guess, not the controller's setpoint.
    lv_label_set_text(g_temp_hint, "PANEL DEFAULT - NOT CONFIRMED BY CONTROLLER");
    lv_obj_set_style_text_color(g_temp_hint, lv_color_hex(kColWarn), 0);
  } else {
    lv_label_set_text(g_temp_hint, "Release finger to confirm");
    lv_obj_set_style_text_color(g_temp_hint, lv_color_hex(kColDim), 0);
  }

  // --- pump ---
  const bool pump_busy = (g_dialog_kind == Dialog::Pump) ||
                         (state.pending() == PendingCommand::Pump);
  if (!pump_busy) {
    lv_obj_set_state(g_pump_switch, LV_STATE_CHECKED, state.pumpOn());
    lv_label_set_text(g_pump_state, state.pumpOn() ? "ON" : "OFF");
  } else if (state.pending() == PendingCommand::Pump) {
    lv_label_set_text(g_pump_state, "WAIT ACK");
  }

  // --- water: red panel stays until Parker reports water_level 0 ---
  if (state.waterLow()) {
    lv_label_set_text(g_water_state, "LOW WATER");
    lv_obj_set_style_bg_color(g_water_panel, lv_color_hex(kColAlarm), 0);
    lv_obj_set_style_text_color(g_water_state, lv_color_hex(0xFFFFFF), 0);
  } else {
    lv_label_set_text(g_water_state, live ? "NORMAL" : "--");
    lv_obj_set_style_bg_color(g_water_panel, lv_color_hex(kColCard), 0);
    lv_obj_set_style_text_color(g_water_state, lv_color_hex(live ? kColText : kColDim), 0);
  }

  // --- full-screen alarm ---
  if (state.alarmActive()) {
    if (lv_obj_is_hidden(g_alarm)) {
      lv_obj_set_hidden(g_alarm, false);
      g_flash_bright = true;
      lv_timer_resume(g_flash_timer);
    }
    // Mist and condensation cause ghost touches, so the ack button stays inert
    // for the first few seconds.
    if (state.alarmDismissAllowed(now_ms)) {
      lv_obj_set_state(g_alarm_button, LV_STATE_DISABLED, false);
      lv_label_set_text(g_alarm_button_label, "ACKNOWLEDGE");
    } else {
      lv_obj_set_state(g_alarm_button, LV_STATE_DISABLED, true);
      const uint32_t elapsed = now_ms - state.alarmStartedMs();
      const uint32_t left = (cfg::kAlarmMinFlashMs > elapsed)
                                ? (cfg::kAlarmMinFlashMs - elapsed + 999) / 1000
                                : 0;
      lv_label_set_text_fmt(g_alarm_button_label, "WAIT %u s", static_cast<unsigned>(left));
    }
  } else if (!lv_obj_is_hidden(g_alarm)) {
    lv_timer_pause(g_flash_timer);
    lv_obj_set_hidden(g_alarm, true);
  }

  g_syncing = false;
}

void log(const char* fmt, ...) {
  for (int i = 0; i < kLogLines - 1; ++i) {
    std::memcpy(g_log[i], g_log[i + 1], kLogWidth);
  }

  char body[kLogWidth] = {};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(body, sizeof(body), fmt, args);
  va_end(args);
  std::snprintf(g_log[kLogLines - 1], kLogWidth, "%s", body);

  if (g_log_label != nullptr) {
    char joined[kLogLines * kLogWidth];
    std::snprintf(joined, sizeof(joined), "%s\n%s\n%s", g_log[0], g_log[1], g_log[2]);
    lv_label_set_text(g_log_label, joined);
  }
}

}  // namespace ui

// Follow-up (not v1): Chinese labels need an lv_font_conv subset of just the
// glyphs used (水分/温度/水泵/水位/确认/取消...). English keeps flash free while
// the protocol is still being brought up.
