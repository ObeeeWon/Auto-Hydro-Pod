# AutoHydroPanel — complete panel firmware, ready to flash

This folder replaces `Mayhaps`. It is the **whole** Auto Hydro panel firmware: display driver, touch, UART link, NDJSON protocol, and the bring-up test button. Nothing is stubbed out.

It goes on the **CrowPanel's own ESP32-S3** (the metal-shielded module on the red board), flashed over the panel's USB-C port. It does not go on the Freenove controller.

Build status on our side, 2026-09-18:

| Check | Result |
|-------|--------|
| `pio run -e panel-mock` | SUCCESS — RAM 26.6%, Flash 26.7% |
| `pio run -e panel` | SUCCESS |
| Host logic tests (`firmware/`, same sources) | 32 / 32 passed |
| **Run on a physical CrowPanel** | **never** — see [`../docs/PANEL_FLASHING_en.md`](../docs/PANEL_FLASHING_en.md) §5 |

That last row matters. The code compiles and the logic is tested, but nobody on our side has the board, so the RGB timing and the GT911 touch address in `display_driver.cpp` are taken from Elecrow's example rather than measured. If the screen comes up white or garbled instead of black, that is progress — the chip is running and only the timing needs correcting. Send a photo and your board revision.

## Flash it — PlatformIO (recommended)

Needs [PlatformIO](https://platformio.org/): `pip install platformio`. The first build downloads the ESP32-S3 toolchain, which takes a while; after that it is about 30 seconds.

```bash
cd AutoHydroPanel
pio run -e panel-mock -t upload   # step 3 below
pio run -e panel -t upload        # step 5 below
```

1. **Unplug the 4-pin UART0 cable from the CrowPanel.** UART0 is shared with the USB-C programming port, so flashing fails while the controller cable is attached.
2. **Physically unplug the Freenove's USB cable,** then plug USB-C into the CrowPanel only. **Both boards are ESP32-S3, so flashing the wrong one succeeds with no error whatsoever** — it also overwrites the controller firmware. Confirm with `pio device list` that exactly one port is present, then name it explicitly:

   ```bash
   pio device list
   pio run -e panel-mock -t upload --upload-port /dev/cu.usbmodemXXXX
   ```

   Every upload prints `MAC: xx:xx:xx:xx:xx:xx`. That value identifies the board — keep a note of the panel's MAC and you can always tell afterwards which chip you programmed.
3. **Flash `panel-mock` first.** It generates its own telemetry, so the panel proves its screen, touch and test indicator with no controller attached.
4. **You should see:** `AUTO HYDRO` in the top bar, `NO LINK` briefly, then values moving — moisture sweeping the 45 / 55 / 60 % band, and a `TEST` chip flashing at roughly 12 s, 25 s, 26 s and 45 s. Touch the pump switch and the temperature slider to confirm the GT911 works. **Once this works the screen is proven, and every later problem is a link problem.**
5. **Then wire UART0** (TX↔RX crossed, common GND) and flash `panel`. Now Parker's JSON has somewhere to land — continue with [`../docs/LATEST_UPDATE.md`](../docs/LATEST_UPDATE.md).

## Flash it — Arduino IDE

Supported, but there are four settings the IDE cannot take from `platformio.ini`, and getting any of them wrong gives you a black screen or a boot loop.

**One-time setup.** Copy `lv_conf.h` from this folder to sit *beside* the lvgl library folder, not inside it and not in the sketch folder:

```
~/Documents/Arduino/libraries/lv_conf.h      # macOS / Linux
Documents\Arduino\libraries\lv_conf.h        # Windows
```

This is LVGL's documented Arduino mechanism. Without it, `lv_font_montserrat_28` and friends are not compiled and the build fails — which is very likely what happened with `Mayhaps`.

Libraries, via Library Manager: `lvgl` 9.6 or newer, `LovyanGFX` 1.2 or newer, `ArduinoJson` 7.x.

**Board settings** — Tools menu, board **ESP32S3 Dev Module**:

| Setting | Value | Why |
|---------|-------|-----|
| PSRAM | **OPI PSRAM** | N4R8 part. The 800×480 framebuffer cannot fit without it; wrong setting means RGB init fails and the board boot-loops |
| Flash Size | **4MB (32Mb)** | |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** | LVGL plus Arduino does not fit the default app slot |
| USB CDC On Boot | **Disabled** | `Serial` must stay on UART0 (the Parker link), not native USB |

Then open `AutoHydroPanel.ino` and upload. Mock mode is the default; for the live build change one line in `app_config.h`:

```c
#define AUTOHYDRO_MOCK 0
```

## What is in here

Flat on purpose — the Arduino IDE requires the sketch and its sources in one folder, and `platformio.ini` points `src_dir` at the folder itself so PlatformIO builds the same files.

| File | Role |
|------|------|
| `AutoHydroPanel.ino` | `setup()` / `loop()`, UART pump, command send, log wiring |
| `display_driver.cpp/.h` | RGB bus, GT911 touch, backlight, LVGL display and tick setup. **This is the file `Mayhaps` was missing** |
| `ui.cpp/.h` | LVGL screens: status bar, moisture, temperature, pump, low-water alarm, TEST chip |
| `app_state.cpp/.h` | Link supervision, ACK bookkeeping, alarm edges, setpoint echo, test-button counting |
| `protocol.cpp/.h` | NDJSON parse and command build |
| `line_assembler.h` | Splits the byte stream into lines |
| `types.h` | `Telemetry` and enums |
| `app_config.h` | Frozen wire constants and the `AUTOHYDRO_MOCK` switch |
| `moisture.h` | Raw ADC to percent |
| `mock_link.cpp/.h` | Synthetic controller. Compiles away to nothing when `AUTOHYDRO_MOCK` is 0 |
| `lv_conf.h` | LVGL config for the Arduino IDE only; PlatformIO uses the flags in `platformio.ini` |

These files are copies of [`../firmware/`](../firmware), which keeps the host unit tests. If you change logic here, change it there too, or the tests stop covering what ships.

Do not edit `app_config.h` constants other than `AUTOHYDRO_MOCK` — they are the wire contract. See [`../docs/INTEGRATION_GUIDE_en.md`](../docs/INTEGRATION_GUIDE_en.md).
