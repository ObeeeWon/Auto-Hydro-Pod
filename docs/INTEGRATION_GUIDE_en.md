# Auto Hydro Controller ↔ Touch HMI — Integration Guide

**Document version:** 1.5  
**Date:** 2026-09-18  
**Status:** v1 wire contract **frozen** after Parker’s replies (verbatim in Appendix C), plus **`setpoint_c`** (§6.2) and a **bring-up test button** (§6.4). Start of bring-up: **`docs/LATEST_UPDATE.md`**.  
**Audience:** Parker (controller firmware) and Feng (touch HMI)  
**Companion:** feasibility notes in `FEASIBILITY_ASSESSMENT_en.md` (background only; **this file is the interface to implement**)

If anything in this guide conflicts with earlier drafts, **this guide wins**.

---

## 0. What changed since the copy you returned — read this first

**Two items need firmware work on your side: a test button (start here) and `setpoint_c`.** Everything else below is either your own answers written into the contract or panel-side work.

| # | Change | Your action | Where |
|---|--------|-------------|-------|
| 1 | **Bring-up test button** — a physical button on your breadboard, reported as optional `test_button` + `test_count`, lights up a TEST chip on the panel | **Do this first:** it proves the whole cable before any sensor exists | §6.4 |
| 2 | **New required telemetry field `setpoint_c` (15–30)** — the temperature setpoint you are *actually holding* | **Add one integer to every telemetry line** | §6.2, §15.1 |
| 3 | Your replies folded in and frozen: 3.3 V logic, 115200, 1 Hz heartbeat, ACK required, actual `pump` 0/1, measured `temperature_c`, higher ADC = wetter, you supply 5 V 1–2 A, probe duty-cycled ~150 ms | Check the wording matches what you meant | §15 |
| 4 | Four new acceptance tests: setpoint echo (A13, A14) and test button (A15, A16) | Included in bring-up | §14 |
| 5 | Panel name is **AUTO HYDRO**; no date or clock on screen (the panel is offline) | None | §2 |

**Why the test button.** The panel firmware is written but neither of us has seen a byte cross the cable yet. One button press that lights up the screen tells us wiring, baud rate, framing and parsing are all correct — and if it does not work, we are debugging one wire instead of the whole system. It is optional and self-removing: the indicator only exists while you send the fields.

**Why `setpoint_c` came up.** The v1 panel firmware is now written and this gap showed up while building it: telemetry carried the *measured* temperature but never the *setpoint*. The panel has no battery-backed memory, so a panel that has just been powered on shows its own default of 22 °C while you might be holding 26 °C — two different targets on one machine, with no way to tell which is real. `setpoint_c` is the temperature equivalent of the `pump` field you already agreed to, and it costs you one integer per line. Full rules in §6.2.

---

## 1. Purpose

We have frozen the architecture: Parker’s C++ controller talks to a 7" Elecrow CrowPanel (ESP32-S3 + LVGL) over a **local UART**, using **one JSON object per line**. No network. The HMI product name on screen is **AUTO HYDRO**.

This document tells each side:

- what it **owns**
- how to **wire** the boards
- the **exact JSON** to send and accept
- how to **develop in parallel** without blocking
- how we will **bring up and accept** the link

Please read §5–§9. Parker’s answers are recorded in **§15**. Those items are now frozen for v1.

---

## 2. Split of work

| Owner | Owns | Does **not** own |
|-------|------|------------------|
| **Parker** | All sensors, pump relay, heater / temperature actuation, alert *decisions*, UART JSON on the controller | Screen layout, touch handling, LVGL |
| **Feng** | CrowPanel firmware: UI, touch, JSON parse/send, alarms as *presentation* | Sensor ADC, relays, closed-loop control |

The panel has almost no free GPIO. **Do not attach sensors to the display.** All probes stay on Parker’s board.

### Features on the glass (v1)

1. Pump ON/OFF switch — command sent only after the operator confirms.
2. Soil **moisture** % (from your 12-bit ADC) plus too-high / too-low status that **you** compute.
3. Target temperature slider 15–30 °C — command sent only after confirm. **Also show measured `temperature_c`.**
4. Full-screen **Low Water** flash when your float switch reports low.

**HMI chrome (v1):** title **AUTO HYDRO** (this system has no steamer — do not brand it Aeroponic Life Support). Status bar shows **LINK OK** / comms fault only. **No date, no clock, no time-set screen** — the panel is offline, so a displayed date would be wrong and we will not add a setter.

Resistive **air humidity** is **not** shown in v1. Parker’s controller will auto-regulate humidity. A humidity *setpoint* slider can be a later revision if both sides want it; it is not in v1 JSON.

---

## 3. Block diagram

```
Parker controller (C++)                          CrowPanel 7" (C++ / LVGL)
sensors, pump, heater                            display + touch only
        │                                              │
        │   UART 115200 8N1, 3.3 V TTL (confirmed)     │
        │   Parker TX ──► Panel RX (IO44)              │
        │   Parker RX ◄── Panel TX (IO43)              │
        │   GND ──────── GND                           │
        │                                              │
        │   telemetry  (1 Hz heartbeat, NDJSON)  ──►   │
        │   command    (only after user confirm) ◄──   │
        │   ack        (MUST in v1)              ──►   │
```

Offline end-to-end. No Wi-Fi, no cloud.

---

## 4. Physical link  (do this before writing clever firmware)

### 4.1 Electrical

| Item | Value | Notes |
|------|-------|--------|
| Connector on panel | **UART0**, HY2.0-4P, silkscreen `UART0` | Same UART as the USB-C CH340. Pins: TX = **IO43**, RX = **IO44** |
| Baud | **115200**, 8 data, no parity, 1 stop | **Frozen.** Parker can do 1200–921600; we stay at 115200 unless both sides change it |
| Logic | **3.3 V TTL both sides** | Parker confirmed 3.3 V. **No level shifter.** Direct 5 V into the ESP32-S3 will damage it |
| Ground | **Common GND is mandatory** | TX/RX without GND is the #1 “JSON never arrives” failure |

### 4.2 Power (Parker will supply the panel)

The 7" panel wants **5 V / 2 A** (USB-C or the UART0 **5 V-in** pin).

Parker will power the panel from the controller at **5 V, 1–2 A** (controller Wi-Fi unused, so budget is tighter). **Prefer 2 A.** 1 A may brown-out the backlight.

**Wiring caution:** CrowPanel’s **BAT** connector is for a **3.7–4.2 V Li-ion cell**, not 5 V. Feed 5 V only into **USB-C** or **UART0 5 V-in**. Do not put 5 V on BAT.

If the backlight flickers or the panel resets under load, add an independent 5 V / 2 A supply and keep only GND + TX + RX between boards.

Follow the **silkscreen** on both boards for pin order. Cross TX↔RX. Never connect 5 V to a 3.3 V pin.

### 4.3 Debug constraint (important)

On the CrowPanel, UART0 is **shared with USB-C**. While Parker’s cable is plugged into UART0:

- Feng **cannot** use the USB serial monitor at the same time
- Unplug Parker’s UART to flash or log the panel over USB

Plan: Feng will put a small on-screen log. Parker should be able to log his own UART independently (second UART, SWD, etc.).

---

## 5. Protocol contract (v1)

| Rule | Detail |
|------|--------|
| Framing | **NDJSON**: one JSON object, then `\n` (LF). No length prefix, no STX/ETX |
| Encoding | UTF-8, **no BOM**, compact (no pretty-print newlines inside the object) |
| Size | Keep each line **&lt; 256 bytes** |
| CRLF | Parker **should** send LF only. The panel **will** accept `\n` and `\r\n` |
| Unknown fields | Ignore. Do not fail the parse |
| Bad line | Drop that line, keep running. **Never reboot** on parse error |
| Cancel | If the operator taps Cancel, the panel sends **nothing** |

Message types in v1:

| `type` | Direction | Required in v1? |
|--------|-----------|-----------------|
| `telemetry` | Parker → panel | **MUST** |
| `command` | Panel → Parker | **MUST** |
| `ack` | Parker → panel | **MUST** |

---

## 6. Telemetry — Parker → panel

Send a telemetry line about **once per second** (heartbeat). **Parker confirmed this.** Moisture hardware only samples every **~30 s** and is **powered down between samples** (probe corrosion if left powered; ~**150 ms** warmup after power-on). Repeat the last `moisture_raw` on the 1 Hz line. A quiet link looks like a dead cable to the UI.

Also send **immediately** when `moisture_alert` or `water_level` **changes**.

### 6.1 Example (copy this shape)

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.0,"setpoint_c":22,"pump":1}
```

### 6.2 Fields

| Field | Type | v1 | Values | Meaning |
|-------|------|----|--------|---------|
| `type` | string | MUST | `"telemetry"` | Discriminator |
| `moisture_raw` | int | MUST | **0–4095** | Capacitive moisture, **12-bit ADC raw**. Do **not** downscale |
| `moisture_alert` | int | MUST | 0 / 1 / 2 | **0** = in band, **1** = too wet, **2** = too dry. **You** compare to the threshold; the panel only displays it |
| `water_level` | int | MUST | 0 / 1 | Float switch: **0** = OK, **1** = low water |
| `temperature_c` | number | MUST | measured °C | Current temperature. Panel **will show** it next to the setpoint |
| `setpoint_c` | number | MUST | **15–30** | The temperature setpoint **you are actually holding**. Same role as `pump`: it makes the panel match the box after a panel reboot |
| `pump` | int | MUST | **0** or **1** | **Actual** pump state: 0 = off, 1 = on. Needed after panel reboot so the switch matches the relay |

**Why `setpoint_c` is required (added in v1.3).** The panel has no battery-backed memory of your setpoint. Without this field, a panel that has just been powered on shows its own default of 22 °C while your controller may be holding 26 °C — two different numbers on the same machine, and nobody can tell which is real. With the field, the panel adopts your value on the first telemetry line.

Rules the panel follows:

- Your echo wins. If somebody changes the setpoint on your side, the panel follows within one second.
- While a panel setpoint command is waiting for its ACK, the echo is ignored, so the number does not flicker.
- For **1 s after** an `ok:true` on a setpoint command, the echo is ignored too. That covers the case where you ACK before you apply. After that second, your value is authoritative again.
- Until the first `setpoint_c` arrives, the panel labels the number **"PANEL DEFAULT - NOT CONFIRMED BY CONTROLLER"** in amber, so it is never mistaken for yours.

Two more **optional, bring-up-only** fields (`test_button`, `test_count`) are described in **§6.4**. Please implement those first — they prove the cable before any sensor is involved.

Do **not** send `humidity_raw` / `humidity_alert`. Those names are retired.

Do **not** send resistive air humidity in v1.

### 6.3 Moisture numbers (so both sides show the same %)

| ADC `moisture_raw` | Soil moisture | Role |
|--------------------|---------------|------|
| 1844 | 45% | Too-dry line |
| ≈ 2252 | 55% | Your control target |
| 2457 | 60% | Too-wet line |

Panel mapping: `percent = round(raw * 100 / 4095)`, clamped 0–100.

Suggested alert logic on **Parker** (panel will not second-guess you):

| Condition | `moisture_alert` |
|-----------|------------------|
| raw &lt; 1844 | **2** (too dry) |
| 1844 ≤ raw ≤ 2457 | **0** |
| raw &gt; 2457 | **1** (too wet) |

**Polarity (Parker confirmed):** **higher ADC = wetter**, lower = drier. The panel will not invert. Parker may still add invert-in-firmware if a particular probe is backwards.

**Probe power:** do **not** leave the capacitive moisture sensor powered 24/7 (corrosion). Power it for the sample only (~150 ms ready), then off. Publish the last integer at 1 Hz. Do not drop the sample interval much — faster sampling wears the probe out sooner.

### 6.4 Bring-up only: physical test button (optional, please implement first)

**Purpose.** Prove the whole chain in one gesture: **you press a button on your breadboard → your controller → UART → the panel lights up.** No sensors, no pump, no relays needed. This is the first thing worth getting working, because until it does, nothing else can be debugged.

Wire a **momentary push button** to any spare GPIO on your board — the green button already on your breadboard is fine. Internal pull-up, other leg to **GND**, debounce ~20–30 ms in firmware. **Do not wire the button to the CrowPanel**; running it through your controller is the entire point of the test.

Then add two optional fields to telemetry:

| Field | Type | Values | Meaning |
|-------|------|--------|---------|
| `test_button` | int | 0 / 1 | **1 while the button is held.** Drives a live lamp on the panel |
| `test_count` | int | monotonic, starts at 0 | **Increment by 1 on each press edge.** Never decrement except on reboot |

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0,"test_button":1,"test_count":3}
```

**Send an extra telemetry frame immediately on the button edge**, same rule as `water_level`. A normal press is ~100–250 ms, which is shorter than the 1 Hz heartbeat, so without the edge frame the press falls between two lines and looks like a dead button.

**Why a counter and not just the 0/1 state.** The counter is the actual proof:

- A press that still slipped between two frames is not lost — the count arrives one heartbeat later and the panel flashes then.
- Press the button **five times** and the panel must read **5**. If it reads 4, frames are being dropped and we have a real link problem to chase, not a mystery.
- A double press shows up as **+2**, so we can tell "two presses" from "one long press".

**What you will see on the panel.** A **TEST** chip appears in the top status bar with a lamp and the count. The lamp lights while the button is held, and latches bright for 800 ms on each counted press so a short tap is visible from across the bench. Every press also writes a line into the on-screen log (`TEST BUTTON +1, count 3`), which gives a countable history if we blink and miss one. If your controller reboots and the counter drops, the panel says so instead of pretending.

The chip **only appears when these fields arrive**. Stop sending them and it disappears — nothing needs to be stripped out of either firmware for production, so please leave the button code in for now.

**Bonus with no protocol change:** the potentiometer already on your breadboard can be wired to the moisture ADC channel as a stand-in for the capacitive probe. Turning the knob sweeps the panel's moisture bar through 0–100% and across the 45 / 55 / 60 % marks, which exercises the analog path and the alert logic before the real probe is in soil.

---

## 7. Commands — panel → Parker

The panel sends a command **only after Confirm**. Cancel = no UART traffic.

Parse by `type == "command"`, then look at which keys are present. A command contains **either** pump **or** temperature, not both.

### 7.1 Pump

```json
{"type":"command","pump":1}
```

| `pump` | Meaning | Panel will send? |
|--------|---------|------------------|
| **1** | Turn pump **ON** | Yes, after confirm |
| **2** | Turn pump **OFF** | Yes, after confirm |
| 0 | No change | **No** — treat as no-op if you ever see it |

This **1 / 2** encoding is the original spec. It is **not** the same as telemetry `pump` (0 / 1 = actual state). Please keep both mappings.

After you actuate the relay, the next telemetry frames should show the new actual `pump` 0 or 1.

### 7.2 Temperature setpoint

```json
{"type":"command","temperature":{"changed":1,"new_temp":23}}
```

| Field | Meaning |
|-------|---------|
| `changed` | **1** = apply `new_temp`. Panel will not send `changed: 0` |
| `new_temp` | Integer **15–30**, unit **°C** |

**MUST** reject `new_temp` outside 15–30 even if the panel is supposed to limit it. Serial corruption is real.

Apply the setpoint on your heater / climate loop. You own closed-loop control; the panel only sends the operator’s target.

Once applied, report it back in every telemetry line as `setpoint_c` (§6.2).

---

## 8. ACK — Parker → panel (MUST)

After a valid `command`, reply:

```json
{"type":"ack","ok":true}
```

If the command is illegal (bad pump value, `new_temp` out of range):

```json
{"type":"ack","ok":false}
```

The panel waits **1 s**. No ACK → status “command not confirmed”; the Switch / slider does **not** stick. The operator can retry.

**Parker confirmed ACK in v1.**

---

## 9. Timing

| Event | Requirement |
|-------|-------------|
| Telemetry heartbeat | **~1 Hz** (0.5–2 Hz is acceptable) |
| Moisture ADC sample | ~30 s typical; sensor unpowered between samples; ~150 ms warmup |
| Alert edge (`moisture_alert` or `water_level` change) | Extra telemetry **immediately** |
| UI link-lost | No telemetry for **2 s** → panel shows comms fault and greys values to `--` |
| Command | Only on Confirm; no spam, no repeat unless the user confirms again |
| ACK | Within **1 s** of the command |

**1 Hz heartbeat is confirmed.** Do not drop to “one frame per moisture sample.”

---

## 10. Parker firmware checklist

Use this as an implementation punch list.

- [ ] **First milestone:** momentary test button on a spare GPIO, debounced, reported as `test_button` + `test_count`, extra frame on the edge (§6.4)
- [ ] UART 115200 8N1, TX/RX crossed, common GND
- [x] Logic level **3.3 V** (confirmed — no shifter)
- [ ] NDJSON: compact JSON + `\n`, ignore unknown keys, survive bad lines
- [x] Telemetry ~1 Hz: last moisture integer repeated; probe powered only while sampling (~150 ms)
- [ ] Telemetry includes `type`, `moisture_raw` (0–4095), `moisture_alert`, `water_level`, **`temperature_c`**, **`setpoint_c` 15–30**, **`pump` 0/1**
- [ ] `moisture_raw` is **not** downsampled
- [x] Polarity on the wire: higher raw = wetter
- [ ] Average/median ADC before publish
- [ ] Parse `command` with `pump` 1/2 and `temperature.new_temp` 15–30
- [ ] Re-validate temperature range in firmware
- [ ] Do not actuate on unconfirmed UI motion — you only see UART after Confirm
- [x] ACK in v1
- [ ] No reboot on parse errors

---

## 11. What the panel will do (so you can predict it)

| Operator action | UART |
|-----------------|------|
| Flip pump switch, then **Cancel** | nothing |
| Flip pump switch, then **Confirm ON** | `{"type":"command","pump":1}` |
| Confirm OFF | `{"type":"command","pump":2}` |
| Drag slider, **Cancel** | nothing |
| Drag slider to 23 °C, **Confirm** | `{"type":"command","temperature":{"changed":1,"new_temp":23}}` |
| Low water overlay dismissed | **nothing** (dismiss is display-only; keep sending `water_level:1` until the tank is OK) |

When `water_level` goes **0 → 1**, the panel flashes **Low Water** again even if the last flash was dismissed.

The panel will **not** turn the pump on or off by itself from moisture. Moisture alerts are display + your own control loop.

---

## 12. Parallel development (do not wait for the other board)

You can finish your side with a USB-UART adapter and a laptop.

### 12.1 Parker without the panel

1. Print telemetry to your debug UART or a USB-serial dongle.
2. Paste a command line into a serial terminal and confirm the pump/heater reacts.
3. Optional: run the Python stub in **Appendix B** as a fake panel.

### 12.2 Feng without Parker’s board

Feng will mock your telemetry at 1 Hz (including a 30 s moisture step) and type commands to a log. Bring-up then is “replace the mock with the real cable.”

Agree the JSON **now**; hardware can arrive later.

---

## 13. Joint bring-up (when both boards are on the bench)

Do these in order. Stop if a step fails.

1. **Power & level** — **3.3 V UART, no shifter.** Panel 5 V from Parker (prefer 2 A into USB-C or UART0 5 V-in, **not** BAT).
2. **GND first**, then TX/RX. Panel powered. Parker powered.
3. **Parker TX → panel**: panel status **LINK OK**, moisture % moves when you change `moisture_raw` (or inject test values).
4. **Heartbeat**: unplug TX for 3 s → panel **comms fault**; reconnect → values return.
5. **Pump ON confirm** → relay on, telemetry `pump:1`.
6. **Pump OFF confirm** → relay off, telemetry `pump:0`.
7. **Pump Cancel** → relay unchanged, no command on the line.
8. **Temp confirm 15 and 30** → accepted. Inject 14 or 31 on a test command → Parker **rejects**.
9. **`water_level:1`** → full-screen Low Water. Dismiss on panel → flash stops, you still see low in telemetry. **`water_level:0`** then **1** again → flash returns.
10. **`moisture_alert` 1 and 2** → panel too-wet / too-dry text (not only color).
11. Leave running ≥ 30 minutes: heartbeat steady, no UART wedging.

---

## 14. Acceptance tests (v1 done when all pass)

| ID | Test | Pass |
|----|------|------|
| A1 | 1 Hz telemetry, valid JSON lines | Panel LINK OK |
| A2 | `moisture_raw` 1844 / 2252 / 2457 | UI ≈ 45% / 55% / 60% |
| A3 | `moisture_alert` 2 then 0 then 1 | Dry / OK / wet status |
| A4 | Pump confirm ON/OFF | Relay matches; telemetry `pump` 0/1 |
| A5 | Pump or temp Cancel | No UART command; hardware unchanged |
| A6 | Temp confirm 23 | Setpoint 23 °C applied |
| A7 | Out-of-range `new_temp` | Parker ignores / NACK |
| A8 | `water_level` 0→1 | Flash; dismiss; 1 again flashes |
| A9 | TX disconnected 3 s | Comms fault; recover on reconnect |
| A10 | Garbage line then good JSON | Parker and panel both keep running |
| A11 | Command then ACK within 1 s | Switch / setpoint sticks only after `ok:true` |
| A12 | `temperature_c` in telemetry | Panel shows measured °C next to setpoint |
| A13 | Boot the panel while you hold 26 °C | Panel shows **26** within a second, amber "not confirmed" note disappears |
| A14 | Change the setpoint on your side to 19 °C | Panel slider and big number follow to **19** |
| A15 | **Do this one first.** Press the test button 5 times, slowly | TEST chip flashes on each press and reads **5**; log shows five lines. Count must match exactly |
| A16 | Hold the test button for 3 s | Lamp stays lit the whole time, count goes up by **1**, not more |

---

## 15. Parker replies (frozen 2026-09-17)

Parker’s original wording is copied in **Appendix C** (the annotated copies he returned are no longer kept as a separate folder).

| # | Question | Parker | v1 decision |
|---|----------|--------|-------------|
| 1 | UART logic level | **3.3 V.** Baud 1200–921600 is possible; 115200 is OK | **115200 8N1, 3.3 V, no shifter** |
| 2 | 1 Hz telemetry while moisture samples ~30 s | **Yes.** Last reading stored as int and resent at 1 Hz. Probe **must be powered down** between samples (corrosion). ~**150 ms** to ready after power-on. Faster sampling shortens probe life | **1 Hz heartbeat, 2 s link-lost.** Keep ~30 s sample interval |
| 3 | Actual `pump` 0/1 in telemetry | **Yes** | **MUST** |
| 4 | `ack` | **Yes** | **MUST** |
| 5 | Measured `temperature_c` | **Yes** (also in feasibility Ans10) | **MUST.** Panel shows measured °C |
| 6 | Higher raw = wetter? | **Yes** (higher = wetter, lower = drier). Not 100% sure on every unit; can invert in firmware. Probe span can be trimmed in the 0–4096 range | Panel does **not** invert |
| 7 | Panel power | From Parker: **5 V, 1–2 A** via “onboard battery connections”; Wi-Fi unused so current is reduced | Feed **USB-C or UART0 5 V-in only**, not the Li-ion **BAT** pin. Prefer **2 A** |
| 8 | Air humidity on HMI | Useful but not required. MCU will auto-regulate humidity. Optional later: humidity **setpoint** on the panel if Feng agrees | **v1: not on screen, no humidity JSON.** Possible v1.1 |

### 15.1 One change requested back (v1.3)

Writing the panel firmware surfaced a gap that was not in your list: telemetry carried the **measured** temperature but not the **setpoint**, so after a panel power cycle the two devices could show different targets. Please add **`setpoint_c`** to every telemetry line (§6.2). It is one integer and it is the temperature equivalent of the `pump` field you already agreed to.

Enclosure (feasibility Ans5): CrowPanel acrylic back plate is present; Parker still wants a **full enclosure**. Keep that as a hardware task, not a protocol item.

You do **not** need to implement UI, LVGL, or touch. You do need a clean UART JSON implementation matching this file.

---

## 16. Quick “do / don’t”

**Do**

- Send full 12-bit `moisture_raw` (0–4095)
- Heartbeat at ~1 Hz
- Decide alerts on the controller
- Re-check 15–30 °C in firmware
- Share GND

**Don’t**

- Scale ADC down to 0–100 or 10-bit “to make JSON smaller”
- Send a command-style `pump: 0` meaning “no change” in telemetry (telemetry `pump` is actual 0/1)
- Attach probes to the CrowPanel
- Drive the panel UART at 5 V (both sides are 3.3 V)
- Pretty-print JSON across multiple lines
- Leave the moisture probe powered 24/7
- Put 5 V on the CrowPanel Li-ion **BAT** connector

---

## Appendix A — Example traces

Heartbeat (repeat; moisture may stay constant):

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0}
```

Too dry:

```
{"type":"telemetry","moisture_raw":1500,"moisture_alert":2,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":1}
```

Low water:

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":1,"temperature_c":22.5,"setpoint_c":22,"pump":0}
```

Operator turns pump on, then you ACK and mirror state:

```
{"type":"command","pump":1}
{"type":"ack","ok":true}
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":1}
```

Operator sets 25 °C:

```
{"type":"command","temperature":{"changed":1,"new_temp":25}}
{"type":"ack","ok":true}
```

---

## Appendix B — Tiny Python fake panel (optional)

Run on a laptop USB-UART adapter to exercise Parker’s firmware without the CrowPanel. Adjust `PORT`.

```python
#!/usr/bin/env python3
import json, sys, time, threading
try:
    import serial
except ImportError:
    print("pip install pyserial")
    sys.exit(1)

PORT, BAUD = "/dev/ttyUSB0", 115200  # Windows: "COM3"

def reader(ser):
    buf = b""
    while True:
        buf += ser.read(1)
        if b"\n" not in buf:
            continue
        line, buf = buf.split(b"\n", 1)
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line.decode("utf-8"))
        except Exception as e:
            print("BAD LINE", line, e)
            continue
        print("FROM PARKER", msg)

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    threading.Thread(target=reader, args=(ser,), daemon=True).start()
    print("Commands:  on | off | temp N | quit")
    while True:
        cmd = input("> ").strip().lower()
        if cmd in ("quit", "q"):
            break
        if cmd == "on":
            body = {"type": "command", "pump": 1}
        elif cmd == "off":
            body = {"type": "command", "pump": 2}
        elif cmd.startswith("temp "):
            n = int(cmd.split()[1])
            body = {"type": "command", "temperature": {"changed": 1, "new_temp": n}}
        else:
            print("unknown")
            continue
        ser.write((json.dumps(body, separators=(",", ":")) + "\n").encode("utf-8"))
        print("SENT", body)

if __name__ == "__main__":
    main()
```

---

## Appendix C — Parker’s replies, verbatim (2026-09-11 / 2026-09-17)

Copied from the annotated files he returned. Spelling left as written. These answers are already folded into §15; this appendix is the archive now that the separate folder is gone.

### C.1 Integration-guide checklist (his inline notes on §15)

| # | Question | Parker’s note |
|---|----------|----------------|
| 3 | Will v1 telemetry include actual `pump` 0/1? | Yes |
| 4 | Will v1 include `ack`? | Yes |
| 6 | Is `moisture_raw` already “higher = wetter”? | Im not 100% sure, but I can add code to make it inverted, so Yes |
| 8 | Air humidity on the HMI in a later revision? | Yes/Later |

Questions 1, 2, 5, 7 were answered in the feasibility file below rather than on this table.

### C.2 Feasibility answers (end of his annotated assessment)

Ans1: UART Communications level will happen at 3.3V with the BAUD rate being negotiable (does 115,200 BAUD work for you, the controller can handle 1200 to 921,600 BAUD).

Ans4: The Panel will be powered using its onboard Battery connections Supplying 5V at 1A to 2A (current reduced due to the controller not using Wi-Fi capibilities)

Ans5: The specific model of Crowpanel we plan to use comes with an acrylic back plate to protect the circuitry from accidental short circuits, However I too recommend creating a more robust case that fully encloses the device.

Ans6: The data from the previous scan can be stored as an integer within the code on the microcontroller and sent back out ever 1Hz, but due to the moisture sensors suffering from corrosion when powered constantly, they must be powered down when not in use, and require roughly 150mS to return to a ready state after power is returned. The time between readings can be reduced, but at the cost of quicker component degredation.

Ans7: Shwoing the resistive humidity output can be a useful addition, but is not required, However if Feng is in agreement and willing, we can adjust both of our codes so the Humidity set-point can be adjusted. Or just leave the display of that information out entirely, the microcontroller within the planter will attempt to automatically monitor and adjust the humidity at a constant rate.

Ans8: You have it correct, the higher the reading from the moisture sensor, the more wet it is, and vice versa, the lower the reading the dryer it is. (But the sensors can be can be manually adjusted to give larger steps or chunks of the 0-4096 range)

Ans9: Acknowledgements would make communications easier and less buggy during use, while also allowing for self managed debugging (Smart man for thinking of this Mr.Feng)

Ans10: The measured temperature display can be done, and is also an amazing yet simple little addition (Double points for Mr.Feng)

Revision note from Parker (his v2.2): All Priority0 dependancies have been met, Priority1 dependancies have been met(pending viewership), and Priority2 dependancies have been met but can be cancelled if workload becomes too-much.

---

## Revision history

| Version | Date | Notes |
|---------|------|-------|
| 1.0 | 2026-09-11 | First integration contract: UART + NDJSON v1, moisture ADC, 1 Hz heartbeat, command/ack, bring-up and acceptance |
| 1.1 | 2026-09-17 | On-screen name **AUTO HYDRO**; no date/clock on the status bar (offline, no time-set UI) |
| 1.2 | 2026-09-17 | Parker replies frozen: 3.3 V, 115200, 1 Hz heartbeat, ACK MUST, `pump` + `temperature_c` MUST, higher ADC = wetter, Parker supplies 5 V 1–2 A (not BAT pin), moisture probe duty-cycled (~150 ms). Humidity display deferred |
| 1.3 | 2026-09-17 | Gap found while writing the v1 panel firmware: telemetry gains **`setpoint_c` (MUST, 15–30)** so the panel matches the controller's real setpoint after a reboot. Documents echo precedence, the 1 s post-ACK grace window, and the amber "not confirmed" note. Acceptance items A13/A14 added |
| 1.4 | 2026-09-18 | **Bring-up test button** (§6.4): optional `test_button` / `test_count`, edge frame required, press counter as the proof of an unbroken link. Panel shows a self-removing TEST chip and logs every press. Acceptance items A15/A16; first item on Parker's checklist |
| 1.5 | 2026-09-18 | Parker’s annotated replies copied verbatim into **Appendix C**; `docs/parker confirmed/` removed. Bring-up one-pager for Parker: **`docs/LATEST_UPDATE.md`** |
