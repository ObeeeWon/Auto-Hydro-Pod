# Auto Hydro

Offline 7" touch HMI for a pump-based hydroponic controller. The panel talks to the box over **UART**, not Wi-Fi: one compact JSON object per line, 115200 8N1, 3.3 V.

屏侧固件 + 对接协议。控制板（传感器、水泵、加热）由 Parker 负责；本仓库不接探头。

## What is on the glass (v1)

- **AUTO HYDRO** status bar — `LINK OK` / comms fault only. No clock (the panel is offline).
- Soil **moisture** % from a 12-bit ADC (0–4095 on the wire), with too-dry / too-wet status.
- **Target temperature** slider 15–30 °C, plus measured °C. Commands go out only after Confirm, and stick only after an ACK.
- **Pump** ON/OFF switch, same confirm / ACK rules. The switch follows the *actual* relay state from telemetry.
- Full-screen **LOW WATER** alarm on the float-switch rising edge (3 s ignore-touch, then Acknowledge).
- Bring-up **TEST** chip: a physical button on the controller lights the panel and increments a press counter, proving the cable before any sensor is involved.

## Hardware

| Side | Board |
|------|--------|
| Display | [Elecrow CrowPanel ESP32 HMI 7.0"](https://www.amazon.ca/dp/B0F8NFFH29) (ESP32-S3-WROOM-1-N4R8, 800×480, GT911) |
| Controller | Parker’s C++ board — sensors, pump relay, heater. **Do not attach probes to the panel.** |

Power the panel with **5 V into USB-C or UART0 5 V-in**, never the Li-ion BAT pin. Prefer 2 A.

## Protocol (frozen)

NDJSON over UART0 (panel IO43 TX / IO44 RX). Three message types: `telemetry` (~1 Hz), `command`, `ack`.

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0}
```

The contract — pinout, field table, ACK rules, bring-up tests — is in **[`docs/INTEGRATION_GUIDE_en.md`](docs/INTEGRATION_GUIDE_en.md)** (English, send this to Parker). Chinese copy: [`docs/INTEGRATION_GUIDE_zh.md`](docs/INTEGRATION_GUIDE_zh.md). If anything conflicts with earlier drafts, the integration guide wins.

**Parker, first evening on the bench:** [`docs/LATEST_UPDATE.md`](docs/LATEST_UPDATE.md) — wire the UART, add the breadboard test button, press five times. The panel must count to 5.

**Screen black on the first attempt?** [`docs/PANEL_FLASHING_en.md`](docs/PANEL_FLASHING_en.md) — one measurement that tells the three possible causes apart, and an honest note that the display driver has never run on hardware. Chinese copy: [`docs/PANEL_FLASHING_zh.md`](docs/PANEL_FLASHING_zh.md).

**Flashing the panel:** use [`AutoHydroPanel/`](AutoHydroPanel) — the complete firmware in one flat folder that builds under both PlatformIO and the Arduino IDE. Do not use the `Mayhaps` sketch; it has the display driver and the whole test-button feature deleted.

## Repo layout

```
AutoHydroPanel/  Flash this. Complete firmware, flat folder, PlatformIO or Arduino IDE
firmware/        Same sources as src/ + include/, and the host unit tests
docs/            Integration guide, feasibility notes, LATEST_UPDATE, panel flashing (en / zh)
```

Logic that is not drawing (line splitting, JSON parse, link / ACK / alarm state) builds natively and is unit-tested on the host.

## Build

Needs [PlatformIO](https://platformio.org/) (`pip install platformio`). First build downloads the ESP32-S3 toolchain.

```bash
cd firmware
pio test -e native                     # logic tests, no hardware
pio run -e crowpanel7-mock -t upload   # synthetic telemetry — screen and touch only
pio run -e crowpanel7 -t upload        # live UART to Parker’s controller
```

UART0 is both the Parker link **and** the USB serial port on this board. The firmware never prints to it; debug output is the on-screen log.

## Split of work

| | Owns |
|--|------|
| **Parker** | Sensors, pump, heater, alert *decisions*, UART JSON on the controller |
| **This repo** | CrowPanel UI, touch, JSON parse/send, alarm *presentation* |
