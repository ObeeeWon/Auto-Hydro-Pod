# Latest update — start here (Parker)

**Date:** 2026-09-18  
**Audience:** Parker (controller firmware), for the first joint UART bring-up  
**Full contract:** [`INTEGRATION_GUIDE_en.md`](INTEGRATION_GUIDE_en.md) v1.5 (this file does not replace it)

This is the shortest path to a proven cable: **press a button on your breadboard → your MCU → UART → the panel lights up.** No soil probe, no pump, no heater. Until this works, we are debugging one wire, not the whole system.

中文版在本文后半。

---

## 1. Wire the two boards

| Signal | Parker | CrowPanel UART0 (HY2.0-4P) |
|--------|--------|----------------------------|
| TX | MCU TX | **RX = IO44** |
| RX | MCU RX | **TX = IO43** |
| GND | GND | GND (**required**) |
| 5 V (optional) | 5 V, 1–2 A | USB-C or UART0 **5 V-in only**. Never the Li-ion **BAT** pin |

- **3.3 V TTL**, **115200 8N1**. No level shifter.
- Cross TX↔RX. Common GND first, then the data lines.
- UART0 on the panel is shared with USB-C. While your cable is plugged in, Feng cannot use the USB serial monitor. Debug on the panel is the **on-screen log**.

## 2. Wire the test button (your breadboard)

Use the green momentary button already on the white breadboard.

- One side → spare GPIO with **internal pull-up**
- Other side → **GND**
- Debounce ~20–30 ms in firmware
- **Do not wire this button to the CrowPanel.** The whole point is that the press goes through *your* controller.

Optional, no protocol change: the potentiometer on the same breadboard can feed the moisture ADC channel. Turning it sweeps the panel bar 0–100% across the 45 / 55 / 60 % marks before the real probe is in soil.

## 3. What to send

NDJSON: **one compact JSON object per line**, UTF-8, trailing `\n`, each line **< 256 bytes**.

Heartbeat ~**1 Hz**. On a **button edge**, send an extra line immediately (a press is shorter than 1 s; otherwise it vanishes between heartbeats).

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0,"test_button":1,"test_count":3}
```

| Field | You send | Notes |
|-------|----------|--------|
| `type` | `"telemetry"` | Discriminator |
| `moisture_raw` | 0–4095 | 12-bit ADC, **do not downscale**. Higher = wetter |
| `moisture_alert` | 0 / 1 / 2 | 0 = in band, 1 = too wet, 2 = too dry. You decide |
| `water_level` | 0 / 1 | 1 = low |
| `temperature_c` | measured °C | Shown next to the setpoint |
| `setpoint_c` | 15–30 | The setpoint **you are actually holding** (needed after a panel reboot) |
| `pump` | 0 / 1 | **Actual** relay. Not the same encoding as the command |
| `test_button` | 0 / 1 | **1 while held** |
| `test_count` | 0, 1, 2, … | **+1 on each press edge.** Never decrease except on reboot |

Minimum for the first evening: any legal telemetry line that includes **`test_button` and `test_count`**. Other fields can be dummies (`moisture_raw` still must be present).

When a command arrives, reply within **1 s**:

```json
{"type":"ack","ok":true}
```

Pump command uses **1 = ON, 2 = OFF**. Temperature command: `{"type":"command","temperature":{"changed":1,"new_temp":23}}`.

## 4. What you will see on the panel

1. Status bar right side: **NO LINK** → **LINK OK** within ~1 s of your first line.
2. A **TEST** chip appears in the top bar (hidden until these fields arrive).
3. Hold the button: the chip turns **green**, lamp on.
4. Tap: chip flashes ~800 ms; number goes **+1**; bottom log: `TEST BUTTON +1, count N`.
5. Press **five times**, slowly. The chip **must read 5** and the log must have five lines. **4 means we are dropping frames.**
6. Hold 3 s: lamp stays on the whole time; count **+1 only**.

Stop sending `test_button` / `test_count` later and the chip disappears by itself. Leave the GPIO code in.

## 5. Pass / fail (do this first)

| ID | You do | Pass |
|----|--------|------|
| A1 | 1 Hz valid JSON | **LINK OK** |
| A15 | Press 5 times, slowly | TEST reads **5**; five log lines |
| A16 | Hold 3 s | Lamp stays lit; count **+1** only |
| A9 | Unplug TX 3 s | **COMM FAULT**; reconnect recovers |

Full list: integration guide §14 (A1–A16).

## 6. After the button works

Then, in this order: moisture numbers (1844 ≈ 45%, 2252 ≈ 55%, 2457 ≈ 60%) → pump confirm ON/OFF → temperature confirm 15 and 30 → `water_level` 0→1 full-screen alarm.

If anything here conflicts with a longer doc, **`INTEGRATION_GUIDE_en.md` wins**.

---

# 最新更新 — 从这里开始（给 Parker）

**日期：** 2026-09-18  
**完整约定：** [`INTEGRATION_GUIDE_en.md`](INTEGRATION_GUIDE_en.md) v1.5（本页不能替代那份文件）

最短路径： **在面包板上按一下按钮 → 你的 MCU → UART → 屏亮。** 不需要土壤探头、水泵、加热。这一步没通之前，我们只在查一根线，而不是整套系统。

## 1. 两板接线

| 信号 | Parker | CrowPanel UART0（HY2.0-4P） |
|------|--------|------------------------------|
| TX | MCU TX | **RX = IO44** |
| RX | MCU RX | **TX = IO43** |
| GND | GND | GND（**必须**） |
| 5 V（可选） | 5 V，1–2 A | 只进 USB-C 或 UART0 的 **5 V 输入**。不要进锂电池 **BAT** |

- **3.3 V TTL**，**115200 8N1**，不用电平转换。
- TX↔RX 交叉。先共地，再接数据线。
- 屏的 UART0 与 USB-C 共用。你的线插着时，Feng 不能开 USB 串口监视器。屏上的调试在**底部日志**。

## 2. 测试按钮（你的面包板）

用白色面包板上现成的绿色自复位按钮。

- 一端 → 空闲 GPIO，**内部上拉**
- 另一端 → **GND**
- 固件消抖约 20–30 ms
- **不要接到 CrowPanel 上。** 信号必须走你的控制板，这才是测试的意义。

可选、不改协议：同一块板上的电位器接到水分 ADC，旋钮即可把屏上水分条从 0 扫到 100%，经过 45 / 55 / 60% 刻度。

## 3. 发什么

NDJSON：**每行一条紧凑 JSON**，UTF-8，行尾 `\n`，每行 **< 256 字节**。

心跳约 **1 Hz**。**按钮跳变时立刻再发一帧**（按一下短于 1 秒，不补帧就会丢在两个心跳之间）。

字段表、样例见上文英文 §3。第一个晚上的最低要求：任意合法 telemetry 里带上 **`test_button` 和 `test_count`**。`moisture_raw` 仍必须有。命令到达后 **1 s 内**回 `{"type":"ack","ok":true}`。水泵命令 **1=开、2=关**。

## 4. 屏上会看到什么

1. 状态栏右侧：约 1 秒内 **NO LINK** → **LINK OK**
2. 顶部出现 **TEST** 标签（没收到这两个字段时它不存在）
3. 按住：标签变绿，灯亮
4. 点按：闪约 800 ms，数字 **+1**，底部日志 `TEST BUTTON +1, count N`
5. **慢慢按 5 次，必须显示 5。** 显示 4 就是在丢帧
6. 长按 3 秒：灯一直亮，计数只 **+1**

以后停发这两个字段，标签会自己消失。GPIO 代码先留着。

## 5. 先过这四条

A1 心跳 → LINK OK。A15 按 5 次 → 读数 5。A16 长按 3 s → 只 +1。A9 拔 TX 3 s → COMM FAULT，插回恢复。完整表见对接指南 §14。

## 6. 按钮通了之后

水分刻度 → 水泵确认开关 → 温度 15/30 → 低水位全屏告警。与长文档冲突时，**以 `INTEGRATION_GUIDE_en.md` 为准**。
