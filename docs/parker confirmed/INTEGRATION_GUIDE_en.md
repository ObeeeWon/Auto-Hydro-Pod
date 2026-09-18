# Parker Controller ↔ Touch HMI — Integration Guide

**Document version:** 1.0  
**Date:** 2026-09-11  
**Status:** Working contract for parallel development and bring-up  
**Audience:** Parker (controller firmware) and Feng (touch HMI)  
**Companion:** feasibility notes in `FEASIBILITY_ASSESSMENT_en.md` (background only; **this file is the interface to implement**)

If anything in this guide conflicts with earlier drafts, **this guide wins**.

---

## 1. Purpose

We have frozen the architecture: Parker’s C++ controller talks to a 7" Elecrow CrowPanel (ESP32-S3 + LVGL) over a **local UART**, using **one JSON object per line**. No network.

This document tells each side:

- what it **owns**
- how to **wire** the boards
- the **exact JSON** to send and accept
- how to **develop in parallel** without blocking
- how we will **bring up and accept** the link

Please read §5–§9 and reply to the checklist in **§15**. Those answers are the last items that affect firmware.

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
3. Target temperature slider 15–30 °C — command sent only after confirm.
4. Full-screen **Low Water** flash when your float switch reports low.

Resistive **air humidity** is **not** shown in v1. You may keep reading it for your own control; do not send it unless we add a field later.

---

## 3. Block diagram

```
Parker controller (C++)                          CrowPanel 7" (C++ / LVGL)
sensors, pump, heater                            display + touch only
        │                                              │
        │   UART 115200 8N1, 3.3 V TTL                 │
        │   Parker TX ──► Panel RX (IO44)              │
        │   Parker RX ◄── Panel TX (IO43)              │
        │   GND ──────── GND                           │
        │                                              │
        │   telemetry  (1 Hz heartbeat, NDJSON)  ──►   │
        │   command    (only after user confirm) ◄──   │
        │   ack        (recommended)             ──►   │
```

Offline end-to-end. No Wi-Fi, no cloud.

---

## 4. Physical link  (do this before writing clever firmware)

### 4.1 Electrical

| Item | Value | Notes |
|------|-------|--------|
| Connector on panel | **UART0**, HY2.0-4P, silkscreen `UART0` | Same UART as the USB-C CH340. Pins: TX = **IO43**, RX = **IO44** |
| Baud | **115200**, 8 data, no parity, 1 stop | Do not change without both sides agreeing |
| Logic | Panel UART is **3.3 V** | If Parker’s MCU is **5 V** (Uno / Mega class), use a **level shifter**. Direct 5 V into the ESP32-S3 will damage it. If Parker is 3.3 V (ESP32 / STM32 / most ARM), wire straight through |
| Ground | **Common GND is mandatory** | TX/RX without GND is the #1 “JSON never arrives” failure |

**Please tell Feng your UART voltage (3.3 V or 5 V) before the first cable is plugged in.** See §15.

### 4.2 Power (recommended)

The 7" panel wants **5 V / 2 A** (USB-C or the UART0 5 V pin).

**Preferred:** panel on its **own** 5 V / 2 A supply. Between boards, run only **GND + TX + RX**. Do not feed the panel from Parker’s 5 V rail unless you have measured **≥ 2 A headroom** with backlight on.

If you do power the panel from UART0, that pin is **5 V in**, not 3.3 V.

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
| `ack` | Parker → panel | **SHOULD** (strongly recommended) |

---

## 6. Telemetry — Parker → panel

Send a telemetry line about **once per second** (heartbeat). Moisture hardware may only sample every **~30 s**; that is fine — **repeat the last `moisture_raw`**. A quiet link looks like a dead cable to the UI.

Also send **immediately** when `moisture_alert` or `water_level` **changes**.

### 6.1 Example (copy this shape)

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.0,"pump":1}
```

### 6.2 Fields

| Field | Type | v1 | Values | Meaning |
|-------|------|----|--------|---------|
| `type` | string | MUST | `"telemetry"` | Discriminator |
| `moisture_raw` | int | MUST | **0–4095** | Capacitive moisture, **12-bit ADC raw**. Do **not** downscale |
| `moisture_alert` | int | MUST | 0 / 1 / 2 | **0** = in band, **1** = too wet, **2** = too dry. **You** compare to the threshold; the panel only displays it |
| `water_level` | int | MUST | 0 / 1 | Float switch: **0** = OK, **1** = low water |
| `temperature_c` | number | SHOULD | measured °C | Current temperature if you have a sensor. Panel may show it; setpoint is separate |
| `pump` | int | SHOULD | **0** or **1** | **Actual** pump state: 0 = off, 1 = on. Needed after panel reboot so the switch matches the relay |

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

**Polarity:** the table above is **higher ADC = wetter**. If your capacitive probe is inverted (typical bare modules read higher when dry), **invert in your firmware** before putting the value on the wire. The panel will not invert.

**Filtering:** because the UI holds a sample for up to 30 s, please average or median **8–16** ADC reads before you publish `moisture_raw` and compare the band.

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

---

## 8. ACK — Parker → panel (SHOULD)

After a valid `command`, reply:

```json
{"type":"ack","ok":true}
```

If the command is illegal (bad pump value, `new_temp` out of range):

```json
{"type":"ack","ok":false}
```

The panel waits **1 s**. No ACK → status “command not confirmed”. The operator can retry.

If you cannot do ACK in v1, say so in §15. The panel will then treat “command put on the wire” as success, which is weaker.

---

## 9. Timing

| Event | Requirement |
|-------|-------------|
| Telemetry heartbeat | **~1 Hz** (0.5–2 Hz is acceptable) |
| Moisture ADC sample | ~30 s is fine; heartbeat still repeats last raw |
| Alert edge (`moisture_alert` or `water_level` change) | Extra telemetry **immediately** |
| UI link-lost | No telemetry for **2 s** → panel shows comms fault and greys values to `--` |
| Command | Only on Confirm; no spam, no repeat unless the user confirms again |
| ACK | Within **1 s** of the command |

If a 1 Hz heartbeat is genuinely impossible, tell Feng. We will raise link-lost to ≥ 60 s. **Please try 1 Hz** — it is only a short JSON line.

---

## 10. Parker firmware checklist

Use this as an implementation punch list.

- [ ] UART 115200 8N1, TX/RX crossed, common GND
- [ ] Logic level 3.3 V or shifter in place
- [ ] NDJSON: compact JSON + `\n`, ignore unknown keys, survive bad lines
- [ ] Telemetry ~1 Hz with `type`, `moisture_raw` (0–4095), `moisture_alert`, `water_level`
- [ ] `moisture_raw` is **not** downsampled
- [ ] Polarity on the wire: higher raw = wetter, matching 1844 / 2457
- [ ] Average/median ADC before publish
- [ ] `pump` actual state 0/1 in telemetry (SHOULD)
- [ ] Parse `command` with `pump` 1/2 and `temperature.new_temp` 15–30
- [ ] Re-validate temperature range in firmware
- [ ] Do not actuate on unconfirmed UI motion — you only see UART after Confirm
- [ ] ACK (SHOULD)
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

1. **Power & level** — confirm 3.3 V vs 5 V; shifter if needed; panel 5 V / 2 A.
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

---

## 15. Please reply (Parker)

A short yes/no (or value) is enough. These are the remaining P0/P1 items that affect the wire.

| # | Question | Your answer |
|---|----------|-------------|
| 1 | UART logic level of your MCU? | 3.3 V / 5 V |
| 2 | Can you send telemetry ~1 Hz even though moisture samples every ~30 s? | Yes / No (if no, we use 60 s timeout) |
| 3 | Will v1 telemetry include actual `pump` 0/1? | Yes / No |  -  Yes
| 4 | Will v1 include `ack`? | Yes / No |    -  Yes
| 5 | Will v1 include measured `temperature_c`? | Yes / No |
| 6 | Is `moisture_raw` already “higher = wetter” (1844=45%, 2457=60%)? | Yes / I invert in firmware / Need to check |   - Im not 100% sure, but I can add code to make it inverted, so Yes
| 7 | Panel power: independent 5 V / 2 A, or from your board (state available current)? | Independent / From Parker: 2 A |
| 8 | Air humidity on the HMI in a later revision? | No / Later |    -  Yes/Later

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
- Drive the panel UART at 5 V without a shifter
- Pretty-print JSON across multiple lines

---

## Appendix A — Example traces

Heartbeat (repeat; moisture may stay constant):

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"pump":0}
```

Too dry:

```
{"type":"telemetry","moisture_raw":1500,"moisture_alert":2,"water_level":0,"temperature_c":22.5,"pump":1}
```

Low water:

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":1,"temperature_c":22.5,"pump":0}
```

Operator turns pump on, then you ACK and mirror state:

```
{"type":"command","pump":1}
{"type":"ack","ok":true}
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"pump":1}
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

## Revision history

| Version | Date | Notes |
|---------|------|-------|
| 1.0 | 2026-09-11 | First integration contract: UART + NDJSON v1, moisture ADC, 1 Hz heartbeat, command/ack, bring-up and acceptance |
