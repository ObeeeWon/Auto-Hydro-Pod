# Panel black screen — what we know, what we don't, and what to flash

**Date:** 2026-09-18  
**Audience:** Parker and Feng  
**Related:** [`LATEST_UPDATE.md`](LATEST_UPDATE.md) (the bring-up test itself), [`INTEGRATION_GUIDE_en.md`](INTEGRATION_GUIDE_en.md) (the wire contract)

## 0. Summary

Two separate problems, and it matters that they are separate.

1. **The code Parker was given cannot work.** The `Mayhaps` sketch is our firmware with the display driver *and* the entire test-button feature deleted. It cannot light the screen, and even with a working screen the button test in `LATEST_UPDATE.md` could never pass. Replace it with [`AutoHydroPanel/`](../AutoHydroPanel).
2. **We do not yet know why *his* screen is black,** because three different failures look identical from the outside. §2 is one measurement that tells them apart. Please do that before changing any wiring.

And one disclosure we owe Parker: **our display driver has never run on real hardware.** See §5.

## 1. Symptom

- CrowPanel: red power LED on, **screen completely black**, no backlight glow
- Freenove controller: two LEDs lit
- Nothing on the display, with or without the UART cable

Note that the two lit LEDs on the Freenove only mean the controller's own firmware is running. The 4-wire UART0 link between the boards is a **data cable carrying JSON text**, not a display bus. The CrowPanel has **its own ESP32-S3** (the metal-shielded module) and the panel UI runs on that chip, so the controller cannot put anything on the LCD no matter what it sends.

## 2. Do this first: which of three failures is it?

"Power LED on, screen black" is consistent with all three of these, and we cannot tell which from the photos:

| # | Cause | Likelihood |
|---|-------|-----------|
| A | No panel firmware on the chip at all — nothing was flashed, or the upload failed | high |
| B | `Mayhaps` was flashed and crashes in `ui::init()` before anything is drawn | high |
| C | Correct firmware is running, but the RGB timing or backlight config is wrong | real — see §5 |

**The measurement.** Unplug the 4-pin UART0 cable from the CrowPanel, connect USB-C to the CrowPanel only, and open the USB serial monitor at 115200.

- **Silence, no port appears** → case A. The chip has nothing in it, or is not being programmed.
- **`rst:0x... boot:0x...` repeating every second or so** → case B. It is boot-looping.
- **One boot banner, then quiet** → the firmware is running. Case C: the problem is on the display side, not the link.

Our firmware deliberately prints nothing (UART0 belongs to Parker), but the ESP32-S3 ROM bootloader always does, so this test works regardless of which firmware is installed.

## 3. What `Mayhaps` actually is

Not an unfinished port — it is our working firmware with pieces removed. The files are byte-identical to `firmware/` apart from deletions, and their timestamps are *later* than ours, so they were derived from our tree.

| Removed | Evidence | Consequence |
|---------|----------|-------------|
| The display driver implementation | `display_driver.h` is present, `display_driver.cpp` is not. `Mayhaps.ino` line 535 has a comment where init should be: `// (e.g., gfx.begin(), touch.begin(), lv_init(), lv_display_create())` | Nothing configures the RGB bus, the GT911 touch chip, or the **GPIO2 backlight**. `lv_init()` is never called, so `ui::init()` touches LVGL before it exists. Guaranteed black screen |
| **The entire test-button feature** | `types.h` lost `test_button` / `test_count`; `app_state.cpp` lost `updateTestButton()`, `testLampOn()`, `consumeTestPresses()`, `consumeTestCounterReset()`; `app_config.h` lost `kTestLampHoldMs`; `mock_link.h` lost `testButtonAt()` | **The bring-up test cannot pass.** `LATEST_UPDATE.md` asks Parker to press the button five times and watch the panel count to 5. This build has no handling for those fields, so nothing would ever appear — and he would reasonably start suspecting his wiring or his JSON |
| UART receive and the protocol layer | No `protocol.cpp`, no `main.cpp`, no `Serial.read()` in `loop()`; the command callbacks are empty lambdas | No receive and no transmit. A working screen would sit at `NO LINK` forever |

There is also a build-config problem that explains a lot. The real `platformio.ini` **is** in that folder — inside `Mayhaps/data/`. PlatformIO never looks in `data/`, and the Arduino IDE treats `data/` as the SPIFFS upload folder, so **nothing reads it**. That file is what enables `LV_COLOR_DEPTH=16`, `LV_MEM_SIZE`, and Montserrat fonts 20/28/48. The sketch references those three font sizes in **19 places**, while a stock LVGL config enables only Montserrat 14.

**So `Mayhaps` most likely never compiled at all**, failing with something like `'lv_font_montserrat_28' was not declared in this scope`. Parker: please confirm whether your build actually succeeded. If it did, you have a hand-written `lv_conf.h` somewhere and we should know about it.

## 4. What to flash instead

Use [`AutoHydroPanel/`](../AutoHydroPanel). It is the complete firmware — display driver, UART, protocol, and the test button all present — in a single flat folder that builds under **either** PlatformIO or the Arduino IDE. Build instructions are in [`AutoHydroPanel/README.md`](../AutoHydroPanel/README.md).

1. **Unplug the 4-pin UART0 cable from the CrowPanel.** UART0 is shared with the USB-C programming port on this board; flashing with Parker's cable attached fails and can disturb boot.
2. **Plug USB-C into the CrowPanel only,** not the Freenove. Two ESP32 boards means two serial ports, and picking the wrong one flashes the wrong chip.
3. **Flash the mock build first:** `pio run -e panel-mock -t upload`. It generates its own telemetry, so the panel proves its screen, touch and test indicator with no controller attached.
4. **Expect:** `AUTO HYDRO` in the top bar, `NO LINK` briefly, then values moving — moisture sweeping the 45 / 55 / 60 % band, and a `TEST` chip flashing at roughly 12 s, 25 s, 26 s and 45 s. Once you see this, the screen is proven and every later problem is a link problem.
5. **Then** reconnect UART0 (TX↔RX crossed, common GND) and flash the live build: `pio run -e panel -t upload`.

**Do not** flash `Mayhaps` · do not flash panel firmware into the Freenove · do not flash controller code into the CrowPanel.

## 5. Disclosure: the display driver is unverified on hardware

`AutoHydroPanel` compiles and its logic is unit-tested on the host, but **no part of it has ever run on a physical CrowPanel.** Nobody on our side has the board. The pin map and timings in `display_driver.cpp` come from Elecrow's example and the datasheet, as the file's own header comment says:

```1:5:firmware/src/display_driver.cpp
// CrowPanel ESP32 HMI 7.0" (Basic, ASIN B0F8NFFH29) display + touch bring-up.
//
// Pin map is from docs/FEASIBILITY_ASSESSMENT_zh.md §2.1. If the screen stays
// white, verify these against the Elecrow example for your board revision
// before touching anything else — the RGB timing is the usual culprit.
```

So case C in §2 is a genuine possibility, not a formality. The three values most likely to be wrong on a different board revision:

1. **RGB timing** — `hsync`/`vsync` front porch, pulse width, back porch, and `freq_write` (currently 15 MHz). Wrong values give a white, rolling or garbled screen rather than a black one.
2. **GT911 touch address** — `0x5D` on some batches, `0x14` on others. Wrong address means the screen works but touch does not.
3. **Backlight** — driven from GPIO2. If the logic is fine but the panel is dark, a temporary `gfx.setBrightness(255)` immediately after `gfx.init()` separates a backlight problem from a rendering problem.

Parker: if the mock build gives you a white or garbled screen instead of black, that is good news — the chip is running and we only need to correct the timing. Please send a photo and your board revision, and we will match it to Elecrow's example for that revision.

## 6. If it is still black after §4

In order:

1. **Did the upload actually succeed?** Look for `Writing at 0x...` and `Hash of data verified`. A silently failed upload is the most common cause.
2. **Right serial port?** Unplug the Freenove entirely and re-list ports.
3. **Repeat the §2 measurement.** It will now tell you whether you are boot-looping.
4. **PSRAM.** This is an **N4R8**: 4 MB flash, 8 MB **OPI** PSRAM. The 800×480 framebuffer cannot fit without it. `platformio.ini` sets `board_build.arduino.memory_type = qio_opi` and `-DBOARD_HAS_PSRAM`; in the Arduino IDE you must select **OPI PSRAM** by hand or RGB init fails and you boot-loop.
5. **Partition table.** LVGL plus Arduino does not fit the default app partition; we use `huge_app.csv`. In the Arduino IDE choose **Huge APP**.
6. **Then the §5 values.**

One hardware note while debugging: PCLK is on **GPIO0**, which is also the BOOT strap pin. That is Elecrow's design, not our choice, but it means anything holding GPIO0 low at reset drops the board into download mode instead of running your firmware.
