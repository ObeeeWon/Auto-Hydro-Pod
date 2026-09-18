# Auto Hydro — Touch HMI Feasibility Assessment

**Document version:** 2.5 (Parker’s annotated replies archived in §8.1; `parker confirmed/` removed)  
**Date:** 2026-09-18  
**Status:** Internal team review draft  
**Related file:** `display_device_link.txt` → Amazon.ca ASIN **B0F8NFFH29** (Elecrow CrowPanel 7" ESP32-S3). Hardware details: Chinese doc v2.0+.

---

## 0. What changed since the copy you returned — read this first

This file is **background only**. The thing to implement against is `INTEGRATION_GUIDE_en.md` **v1.5**. Start of bring-up: **`docs/LATEST_UPDATE.md`**.

| # | Change | Your action | Where |
|---|--------|-------------|-------|
| 0 | **Bring-up test button** — press a button on your breadboard and it lights up on the panel, proving the whole chain before any sensor is wired | **Start here.** Optional fields `test_button` + `test_count` | Guide §6.4 |
| 1 | **New required telemetry field `setpoint_c` (15–30)** — the temperature setpoint you are *actually holding*. The panel has no battery-backed memory, so without it a freshly powered panel shows its own default 22 °C while you may be holding 26 °C | **Add one integer to every telemetry line** | §5 field table, open issue 10b |
| 2 | `temperature_c` and `pump` moved from "optional" to **MUST** — this now matches what you confirmed, the old wording was stale | None; already agreed | §5 field table |
| 3 | Your answers merged and the matching open issues closed | Check the wording matches what you meant | Open-issues table |
| 4 | The v1 panel firmware is written and building (`firmware/`, PlatformIO + LVGL 9). Panel-side work only | None | — |

---

## 1. Executive Summary

| Item | Conclusion |
|------|------------|
| Overall feasibility | **Feasible** |
| C++ local touch UI | **Feasible** — **LVGL only** on this panel (Qt is not viable) |
| Local JSON communication | **Feasible** (UART0 + NDJSON) |
| Three features (pump / temperature / water level) | **All implementable**; interaction logic is clear |
| Feature 1.1 moisture display | **Feasible**; 0–4096 = **12-bit ADC raw** (do not downscale on the wire); ~30 s sample; band 45–60% ≈ ADC **1844–2457**, target **~55%** |
| No network required | **Aligned** with typical embedded local HMI architecture |

**Recommendation:** Proceed to parallel development against `INTEGRATION_GUIDE_en.md` v1.5. First joint step is the breadboard test button in `docs/LATEST_UPDATE.md`.

---

## 2. Project Background & Responsibilities

### 2.1 System Goal

Provide a **local touch-screen control panel** for **Auto Hydro** (pump-based hydroponic life support — this system has **no steamer**):

- Pump on/off switch (with confirmation dialog)
- Temperature setpoint (15–30°C slider, with confirmation dialog)
- Low-water full-screen alert (touch to dismiss flashing)

Data flows **directly between the hardware controller and the display** — **no network**.

### 2.2 Role Split

| Role | Responsibility | Stack |
|------|----------------|-------|
| **Parker** | Sensor readout, pump/temperature actuation, alert logic, data I/O with display | C++ |
| **UI (Feng)** | Touch UI rendering, interaction, JSON parse/send, alert presentation | C++ (LVGL on CrowPanel) |

### 2.3 Display Device

**Confirmed:** Elecrow CrowPanel ESP32 HMI 7.0" (ASIN B0F8NFFH29) — ESP32-S3-WROOM-1-N4R8, 800×480, GT911 capacitive touch, UART0 HY2.0-4P to Parker. Full pinout, UART0/USB sharing, and LVGL toolchain are in the Chinese assessment v2.0 (`docs/FEASIBILITY_ASSESSMENT_zh.md`). Qt is not usable on this panel.

---

## 3. Recommended Architecture

```
┌─────────────────────┐         UART / USB          ┌─────────────────────┐
│  Parker Controller   │  ◄──── JSON duplex ────►  │  Touch HMI Device    │
│  (C++ firmware)      │      (local, no network)   │  (C++ UI + LVGL/Qt)  │
├─────────────────────┤                             ├─────────────────────┤
│ • Capacitive moisture ADC │  ──► moisture_raw, alerts │ • Show soil moisture % │
│ • Resistive air humidity  │  (not on UI in v1)        │ • Pump switch           │
│ • Float-switch level      │  ──► water_level          │ • Temperature slider    │
│ • Temp sense + pump/heat  │  ◄── setpoint / pump_cmd  │ • Low Water flash       │
└─────────────────────┘                             └─────────────────────┘
```

### 3.1 Communication

| Approach | Notes | Recommendation |
|----------|-------|----------------|
| **UART + NDJSON** | One JSON object per line; easy to debug and log | ⭐⭐⭐ Preferred |
| UART + length-prefixed JSON | Length header + JSON body; good for higher rate | ⭐⭐ |
| USB CDC virtual serial | If Parker board connects via USB | ⭐⭐ Hardware-dependent |

Suggested defaults: **115200 8N1** (can increase to 921600 if both sides agree).

### 3.2 UI Technology Choices

| Platform | Framework | Notes |
|----------|-----------|-------|
| ESP32 / STM32 integrated touch panel | **LVGL** | Lightweight, mature touch support, C++-friendly; can match “functional, slightly retro industrial HMI” style |
| Linux touch panel (RPi, etc.) | **Qt 6 Widgets / QML** | Fast dev, built-in dialogs/sliders; heavier footprint |
| Serial display module (Nextion-like) | Vendor tool + serial protocol | If this is the purchased hardware, **do not** build a separate C++ UI — use vendor editor instead |

**Conclusion:** For “standalone touch panel + custom C++ UI,” the approach is **feasible**; exact framework depends on display hardware.

---

## 4. Feature-by-Feature Feasibility

### 4.1 Pump Switch (Feature 1)

**Requirement:** Single switch for pump on/off; on **finger release**, show confirmation; cancel reverts to previous state.

| Item | Assessment |
|------|------------|
| Feasibility | ✅ Fully feasible |
| Implementation | Bind switch to `touch release`, not `touch down`; keep `pending_value` vs `confirmed_value`; modal confirm/cancel |
| UI → hardware | `1` = on, `2` = off, `0` = no change (see §5.2) |

**UX suggestion:** Dialog copy: “Confirm set pump to [ON/OFF]?”

---

### 4.2 Soil Moisture Display & 0–4096 Range (Feature 1.1)

**Parker confirmed (2026-09-11)** — these answers replace the previous open questions.

| # | Original question | Parker’s answer | UI conclusion |
|---|-------------------|-----------------|---------------|
| 1 | Is 4096 raw or already converted? | **Raw ADC counts.** Can be scaled down, at the cost of accuracy | Keep **full-scale 0–4095 on the wire**. UI converts to %. Do not downscale to 0–1024 or 0–100 in firmware |
| 2 | Sensor type? | Three independent sensors (table below) | Feature 1.1 shows **capacitive soil moisture**, not air humidity. Level is the float switch |
| 3 | Calibration / threshold? | Sample roughly every **30 s**; compare to a preset threshold (**~55%**); **ADC 1844–2457 ≈ 45–60% soil moisture** | Map with that two-point band; alert band = 45–60% |

#### Confirmed sensor set

| Sensor | Type | Data | Used by |
|--------|------|------|---------|
| **Soil moisture** | Capacitive moisture sensor | 12-bit ADC raw **0–4095** (the documented 0–4096 full scale) | Feature 1.1 + `moisture_alert` |
| **Air humidity** | Resistive humidity sensor | Not in the current UI spec | **Not shown in this UI revision**; add a field later if needed (§5.1) |
| **Water level** | Float-switch level sensor | Binary 0/1 | Feature 3 Low Water overlay |

The original requirement said “humidity.” On the hardware, the 0–4096 channel is **moisture**. Label the widget **MOISTURE**, not HUMIDITY, so it is not confused with the resistive air-humidity probe.

#### Why 0–4096?

**4096 = 2¹²** — 12-bit ADC full scale. Valid integers are **0–4095**. This is not a percent.

Parker noted the value *can* be scaled down (e.g. shift to 10-bit 0–1023, or map to 0–100 before sending). **Do not do that.** The 45–60% operating band is only about **613 raw counts** (2457−1844). Downsampling blunts the threshold comparison. Split of labor:

- Parker: report `moisture_raw` **as-is** (0–4095)
- UI: convert to an integer percent for the operator

#### Mapping (from Parker’s operating-band points)

The stated numbers match a linear full-scale map:

| ADC raw | Soil moisture | Check |
|---------|---------------|-------|
| 1844 | 45% | 1844 / 4095 ≈ 45.03% |
| ≈ 2252 | **55% (control target)** | 4095 × 0.55 ≈ 2252 |
| 2457 | 60% | 2457 / 4095 = **60.00%** |

```c
// moisture_raw: 0..4095
static inline int moisture_to_percent(int raw) {
    if (raw < 0)    raw = 0;
    if (raw > 4095) raw = 4095;
    return (int)((raw * 100.0f) / 4095.0f + 0.5f);
}
```

| Parameter | Meaning | Confirmed value |
|-----------|---------|-----------------|
| `raw` | Capacitive moisture 12-bit ADC count | 0–4095 |
| Band low | Too-dry alert line | **45% / ADC 1844** |
| Target | Parker’s comparison threshold | **~55% / ADC ≈ 2252** |
| Band high | Too-wet alert line | **60% / ADC 2457** |

Alert semantics (Parker firmware compares; UI does not re-decide):

| `moisture_alert` | Meaning | Suggested Parker test |
|------------------|---------|----------------------|
| 0 | OK / no change | Inside 45–60% (1844–2457) |
| 1 | Moisture too high | raw > 2457 (> 60%) |
| 2 | Moisture too low | raw < 1844 (< 45%) |

> **Polarity:** Bare capacitive probes often read *higher when drier*. Parker’s 1844→45%, 2457→60% is **direct** (higher = wetter), matching `raw / 4095 × 100`. Implement the UI that way. If the probe is still inverted, invert **in Parker firmware** before sending.

**ADC note for Parker:** A 30 s sample rate means a single spike stays on the UI for half a minute. Average (or median) 8–16 readings on Parker’s side before comparing to 1844/2457.

#### Sampling vs. link timeout

Moisture updates only about every **30 seconds**. That breaks the old “telemetry 2–5 Hz” assumption:

| Item | Approach |
|------|----------|
| Moisture digits | Refresh only on a new sample; **hold the last value** between samples |
| Bar graph | Tick marks at 45% and 60%; target mark at 55% |
| Heartbeat | A slow moisture sample is **not** a lost link. Parker should still send telemetry at about **1 Hz**, repeating the last moisture reading, so the UI can use a 2 s timeout. If hardware can only emit a frame every 30 s, raise the timeout to **≥ 60 s** |

#### UI presentation

- Primary: integer `55 %` (no decimal)
- Optional debug: `RAW 2252` (hide in release)
- `moisture_alert` 1/2 drives both color and text (too high / too low)
- Resistive air humidity is off-screen unless the team later asks for it

#### Still to decide (does not block the % formula)

1. Should resistive air humidity appear on the HMI? If yes, Parker provides that channel’s format (ADC vs already %RH).
2. Can Parker re-send last telemetry at ~1 Hz between 30 s moisture samples? Recommended yes.

---

### 4.3 Temperature Slider (Feature 2)

**Requirement:** 15–30°C, step 1°C; cannot exceed bounds; confirm on finger release; cancel reverts.

| Item | Assessment |
|------|------------|
| Feasibility | ✅ Fully feasible |
| Implementation | Slider `min=15, max=30, step=1`; dialog on release; unit **°C** |
| UI → hardware | If changed: `changed=1` + `new_temp`; else `changed=0` (see §5.2) |

**Safety:** UI hard-limits 15–30; Parker firmware should **re-validate** and reject illegal setpoints.

---

### 4.4 Low Water Full-Screen Alert (Feature 3)

**Requirement:** When water is low, full-screen flashing “Low Water”; touch anywhere stops flash and restores main UI; next alert from hardware flashes again.

| Item | Assessment |
|------|------------|
| Feasibility | ✅ Fully feasible |
| Implementation | Full-screen overlay + timer toggling opacity/background; any touch sets `alarm_dismissed=true`; hardware sending `water_level=1` again clears dismissed |
| Hardware → UI | `1` = low water, `0` = normal |

**Note:** Dismissal is **UI-layer only** — it does not replace refilling on Parker’s side. If Parker needs “user acknowledged alert” feedback, add optional `water_ack: 1` (not in current requirements).

---

## 5. JSON Protocol Proposal (v1 Draft)

Build on original 0/1/2 semantics with **structured JSON** for clarity and extension.

### 5.1 Hardware → UI (Telemetry)

Suggest a **~1 Hz heartbeat** (repeat the last moisture reading). The capacitive moisture probe itself only takes a new sample every **~30 s**. Raise an extra frame immediately on alert edges. Field names `humidity_*` from v1.0 are renamed to `moisture_*`.

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.0,"setpoint_c":22,"pump":1}
```

| Field | Type | Values | Description |
|-------|------|--------|-------------|
| `moisture_raw` | int | 0–4095 | Capacitive moisture **12-bit ADC raw** (do not downscale) |
| `moisture_alert` | int | 0 / 1 / 2 | 0=OK, **1=too high** (>60% / >2457), **2=too low** (<45% / <1844) |
| `water_level` | int | 0 / 1 | Float switch: 0=OK, **1=low water** |
| `temperature_c` | float | MUST (confirmed) | Measured temperature |
| `setpoint_c` | number | MUST (added v1.3) | Setpoint the controller actually holds (15–30), so the panel resyncs after a reboot |
| `pump` | int | MUST (confirmed), 0 / 1 | Actual pump state, so the Switch can resync |
| `humidity_rh` | int/float | optional, omit for now | Resistive air humidity; this UI revision ignores it |

v1.0 `humidity_raw` / `humidity_alert` are **retired**. During bring-up the UI may accept both names; the frozen protocol keeps only `moisture_*`.

### 5.2 UI → Hardware (Command, sent after user confirms)

**Pump change:**

```json
{
  "type": "command",
  "pump": 1
}
```

| `pump` | Meaning |
|--------|---------|
| 0 | No change |
| 1 | Turn pump on |
| 2 | Turn pump off |

**Temperature change:**

```json
{
  "type": "command",
  "temperature": {
    "changed": 1,
    "new_temp": 23
  }
}
```

| `changed` | Meaning |
|-----------|---------|
| 0 | No change |
| 1 | Changed; read `new_temp` (integer 15–30, °C) |

### 5.3 Mapping to Original Requirements

All original numeric semantics are **preserved**; JSON adds field names and `type` for parsing:

| Original requirement | Protocol field |
|---------------------|----------------|
| Pump 0/1/2 | `pump` |
| Temp 0 + new_temp | `temperature.changed` + `temperature.new_temp` |
| Humidity/moisture alert 0/1/2 | `moisture_alert` |
| Water 0/1 | `water_level` |

### 5.4 Communication Rules

1. One JSON object per line (NDJSON), terminated with `\n`.  
2. UI sends commands only after user **confirms**; cancel means no send (equivalent to `pump=0` / `changed=0`).  
3. Parker should ACK commands (optional; suggest v1.1):

```json
{"type":"ack","ok":true}
```

4. Timeout: if **no telemetry for 2 s**, UI shows “Communication lost” and greys moisture/temp to `--`. That requires Parker’s ~1 Hz heartbeat; the moisture *digits* may stay unchanged for 30 s. If a 1 Hz heartbeat is impossible, raise the timeout to ≥ 60 s.

---

## 6. UI Interaction State Machines (Brief)

### 6.1 Pump Switch

```
[confirmed: OFF] ──user toggles ON──► [pending: ON] ──finger up──► dialog
                                        │confirm       │cancel
                                        ▼              ▼
                                  send pump=1    revert to OFF
                                  confirmed=ON
```

### 6.2 Temperature Slider

```
[confirmed: 22°C] ──slide to 25──► [pending: 25] ──finger up──► dialog
                                      │confirm           │cancel
                                      ▼                  ▼
                                send changed=1      revert to 22°C
                                new_temp=25
```

### 6.3 Low Water

```
water_level=0 ──► normal main UI
water_level=1 ──► full-screen flashing "Low Water"
       │user touch
       ▼
alarm_dismissed=true, show main UI
       │Parker sends water_level=1 again
       ▼
alarm_dismissed=false, flash again
```

---

## 7. Risks & Action Items

| # | Risk / Action | Owner | Priority |
|---|---------------|-------|----------|
| 1 | ~~UART 3.3 vs 5 V~~ **Confirmed 3.3 V**, 115200; no shifter | Parker | ✅ Closed |
| 2 | ~~Humidity ADC raw vs scaled?~~ **Confirmed:** 12-bit ADC raw; keep full scale on the wire | Parker | ✅ Closed |
| 3 | ~~Sensor type / dry-wet cal~~ **Confirmed:** capacitive moisture + resistive humidity + float switch; band ADC 1844–2457 = 45–60%, target ~55% | Parker | ✅ Closed |
| 4 | ~~Panel power~~ Parker supplies **5 V, 1–2 A**. **5 V only on USB-C / UART0, never Li-ion BAT.** If backlight browns out, add a dedicated 2 A supply | Both | **Verify on bench** |
| 5 | Enclosure: acrylic back plate exists; still want a full case against condensation / false touch | Both | **P0 (reduced)** |
| 6 | **Heartbeat:** **Confirmed 1 Hz** (last moisture int repeated). Probe unpowered between samples; ~150 ms warmup | Parker | ✅ Closed |
| 7 | Resistive humidity on HMI | **v1 off-screen**; MCU auto-regulates; optional later setpoint | ✅ v1 closed / v1.1 optional |
| 8 | Probe polarity | **Confirmed higher = wetter**; panel does not invert | ✅ Closed |
| 9 | ACK on commands | **Confirmed MUST in v1** | ✅ Closed |
| 10 | Show measured temperature | **Confirmed** — display `temperature_c` | ✅ Closed |
| 10b | Echo the setpoint as `setpoint_c` (otherwise the panel disagrees with the box after a reboot) | Requested while writing the v1 firmware | 🆕 Awaiting Parker |

---

## 8. Conclusion

1. **The overall plan is feasible.** CrowPanel 7" ESP32-S3 + LVGL + UART JSON covers pump, temperature, and low-water alert. Qt is not viable on this panel.  
2. **0–4096 is 12-bit ADC raw**, confirmed by Parker. Keep full scale on the wire (downsampling costs accuracy). UI shows integer percent via `raw / 4095 × 100`. Operating band **45–60% = ADC 1844–2457**, target **~55%**. Capacitive moisture is sampled ~every 30 s; resistive air humidity is a separate sensor and is off-screen in this revision; level is a float switch.  
3. Implement against **`docs/INTEGRATION_GUIDE_en.md` v1.5** (frozen after Parker’s replies; bring-up one-pager: `docs/LATEST_UPDATE.md`).  
4. Remaining hardware watch-outs: panel 5 V feed (not BAT pin, prefer 2 A), full enclosure, moisture probe duty-cycle.

### 8.1 Parker’s replies, verbatim

Copied from the annotated file he returned. Spelling left as written. Already folded into the open-issues table above.

Ans1: UART Communications level will happen at 3.3V with the BAUD rate being negotiable (does 115,200 BAUD work for you, the controller can handle 1200 to 921,600 BAUD).

Ans4: The Panel will be powered using its onboard Battery connections Supplying 5V at 1A to 2A (current reduced due to the controller not using Wi-Fi capibilities)

Ans5: The specific model of Crowpanel we plan to use comes with an acrylic back plate to protect the circuitry from accidental short circuits, However I too recommend creating a more robust case that fully encloses the device.

Ans6: The data from the previous scan can be stored as an integer within the code on the microcontroller and sent back out ever 1Hz, but due to the moisture sensors suffering from corrosion when powered constantly, they must be powered down when not in use, and require roughly 150mS to return to a ready state after power is returned. The time between readings can be reduced, but at the cost of quicker component degredation.

Ans7: Shwoing the resistive humidity output can be a useful addition, but is not required, However if Feng is in agreement and willing, we can adjust both of our codes so the Humidity set-point can be adjusted. Or just leave the display of that information out entirely, the microcontroller within the planter will attempt to automatically monitor and adjust the humidity at a constant rate.

Ans8: You have it correct, the higher the reading from the moisture sensor, the more wet it is, and vice versa, the lower the reading the dryer it is. (But the sensors can be can be manually adjusted to give larger steps or chunks of the 0-4096 range)

Ans9: Acknowledgements would make communications easier and less buggy during use, while also allowing for self managed debugging (Smart man for thinking of this Mr.Feng)

Ans10: The measured temperature display can be done, and is also an amazing yet simple little addition (Double points for Mr.Feng)

Parker’s revision note: All Priority0 dependancies have been met, Priority1 dependancies have been met(pending viewership), and Priority2 dependancies have been met but can be cancelled if workload becomes too-much.

Inline notes on the integration-guide checklist: Q3 pump-in-telemetry **Yes**; Q4 ack **Yes**; Q6 polarity *Im not 100% sure, but I can add code to make it inverted, so Yes*; Q8 humidity *Yes/Later*.

---

## 9. Revision History

| Version | Date | Author | Notes |
|---------|------|--------|-------|
| 1.0 | 2026-09-10 | Feng | Initial draft: feasibility + protocol proposal |
| 2.1 | 2026-09-11 | Feng | Parker confirmed: 4096 is ADC raw (keep full scale); sensors are capacitive moisture / resistive humidity / float switch; moisture ~30 s, band 1844–2457 = 45–60%, target ~55%. Renamed `humidity_*` → `moisture_*`; telemetry heartbeat ~1 Hz. Hardware model (CrowPanel) as in Chinese v2.0 |
| 2.2 | 2026-09-17 | Feng | On-screen product name is **AUTO HYDRO** (no steamer in this system). Status bar shows link status only — **no date**, no clock-set UI (device is offline) |
| 2.3 | 2026-09-17 | Feng | Merged Parker replies from the annotated copies he returned: 3.3 V, 1 Hz, ACK, measured temp, higher ADC = wetter, Parker supplies 5 V 1–2 A; humidity off-screen in v1 |
| 2.4 | 2026-09-17 | Feng | v1 panel firmware built (`firmware/`, PlatformIO + LVGL 9). Telemetry gains **`setpoint_c`**, requested from Parker in integration guide v1.3 §15.1 |
| 2.5 | 2026-09-18 | Feng | Parker’s verbatim answers archived in §8.1; `docs/parker confirmed/` removed. Point bring-up at `docs/LATEST_UPDATE.md` |
