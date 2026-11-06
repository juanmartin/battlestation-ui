# Battlestation UI — minimal bring-up firmware

This is **one firmware image** that proves the five hardware paths we need before
building the real launcher:

1. 1024x600 EK79007 MIPI-DSI display
2. GT911 capacitive touch
3. LVGL 9.5 rendering through the Waveshare BSP / `esp_lvgl_adapter`
4. ES7210 dual-microphone capture with per-channel RMS → dBFS
5. Wi-Fi through the onboard ESP32-C6 using ESP-Hosted

It is deliberately not the final application. The launcher, dashboard and
generative organism come after this probe is stable on the actual board.

## What the screen shows

- Two live mic bars (ES7210 MIC1/MIC2) and numeric dBFS / peak.
- Wi-Fi state, IP and RSSI from the C6 path.
- Live GT911 coordinates and a cyan dot at each touch.
- Adapter FPS counter for the 1024x600 panel.
- A warning that dBFS is **relative digital level, not calibrated dB SPL**.

## Stability fix

The BSP 3.0.1 manifest resolves `espressif/esp_lvgl_adapter 0.6.4`. Espressif
fixed a framebuffer-ownership race in **0.7.3**, so this project carries a local
**0.7.5** override. On this specific unit, 0.7.5 still deadlocks in the adapter's
**rotated partial-refresh** path (`TRIPLE_PARTIAL + 180°`) after ~20–26 s.

Measured isolation:

| Configuration | Result |
| --- | --- |
| Raw `esp_lcd` DSI stress, no LVGL | 70 s stable, 66 fps, zero timeouts |
| LVGL, rotation 0 | 75 s stable |
| LVGL, rotation 180, `TRIPLE_PARTIAL` | deadlock at ~26 s |
| LVGL, rotation 180, `TRIPLE_FULL` | 75 s stable with touch |
| LVGL, rotation 180, `DOUBLE_DIRECT`, adapter 0.7.5 | **386 s stable with touch/audio/LVGL, ~20 fps** |

The default is therefore **`DOUBLE_DIRECT` at 180°** with the local 0.7.5
adapter. `TRIPLE_FULL` is the slower fallback; `TRIPLE_PARTIAL` is fast but
unsafe on this unit. The local override remains:

```text
components/esp_lvgl_adapter/
```

`main/idf_component.yml` points at it with `override_path`. Do not remove that
override until Waveshare widens the BSP dependency from `~0.6`. The 5-second
diagnostic heartbeat (`diag_probe`) reports `heartbeat`, `fps`, `lvgl` state and
temperature, so a recurrence is visible instead of a silent freeze.

## Prerequisites

- macOS Apple Silicon.
- ESP-IDF **v5.5.5** installed at `~/esp/esp-idf-v5.5.5`.
- The board connected to the **USB-C “USB TO UART”** port (CH343P).
- A 2.4 GHz Wi-Fi access point. The C6 does not support 5 GHz.

## Configure

```bash
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
source "$HOME/esp/esp-idf-v5.5.5/export.sh"
cd "$HOME/REPOS/REPROPIOS/BATTLESTATION/battlestation-ui"

idf.py set-target esp32p4
idf.py menuconfig
```

In `Battlestation bring-up` set:

- `Wi-Fi SSID (2.4 GHz only)`
- `Wi-Fi password`
- `ES7210 microphone PGA gain (dB)` — start at 24 dB
- `Display rotation` — **180** (factory orientation). Use 0 only for the diagnostic/alternate physical mounting.
- Touch flags are rotation-dependent in `main.c`: **180° uses no mirrors**, **0° uses mirror x/y**. On this unit, the BSP/09 combination of 180° + mirror x/y maps the visual top-left to (1023,599).

Also verify `Component config → ESP32-P4` matches the silicon you actually
have. The default is rev3.x. For rev1.x, do **not** flash the default image;
see “Silicon revision” below.

### Wi-Fi credentials without committing them

Copy the template and edit it locally:

```bash
cp sdkconfig.wifi.local.example sdkconfig.wifi.local
$EDITOR sdkconfig.wifi.local
```

`sdkconfig.wifi.local` is gitignored. Build with it last so it overrides the
empty defaults:

```bash
idf.py -B build-wifi \
  -D SDKCONFIG="$PWD/sdkconfig.wifi.build" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults;sdkconfig.wifi.local" \
  set-target esp32p4 build
idf.py -B build-wifi -D SDKCONFIG="$PWD/sdkconfig.wifi.build" \
  -p /dev/cu.usbmodemXXXX flash monitor
```

## Build

```bash
idf.py build
```

The first build downloads the BSP, LVGL 9.5 and ESP-Hosted. Do not run several
first-time `idf.py` builds in parallel: they share the Component Manager cache
and can corrupt it. Build examples one at a time.

## Flash and monitor

Use the CH343P UART port. It presents itself as one of:

```text
/dev/cu.usbmodem*        # macOS built-in CDC driver
/dev/cu.wchusbserial*    # WCH CH34x driver, if installed
```

Find it with:

```bash
ls /dev/cu.*
```

Then:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Exit the monitor with `Ctrl+]`.

If flashing fails with `Failed to write to target RAM` on the built-in CDC
port, install WCH’s CH34x driver 2.1 or flash through the **other USB-C**
(native P4 USB Serial/JTAG, usually `/dev/cu.usbmodem*` with a different
suffix). The Type-A port is host-only and cannot flash the board.

### Manual download mode

If auto-reset does not work:

1. Hold `BOOT`.
2. Press and release `RESET`.
3. Release `BOOT`.
4. Run the flash command again.

## Silicon revision (important)

Read the chip before choosing the image:

```bash
esptool.py --chip esp32p4 -p /dev/cu.usbmodemXXXX chip_id
```

Look for `Chip is ESP32-P4 (revision vX.X)`.

- **rev3.x** (v3.0+): build as-is (`CONFIG_ESP32P4_REV_MIN_300=y`).
- **rev1.x**: rebuild with the rev1.3 overlay:

```bash
idf.py fullclean
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
       -D SDKCONFIG="$PWD/sdkconfig.rev1_3" build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Rev1.x and Rev3.x images are not boot-compatible. A mismatch normally fails in
the bootloader with a chip-revision range error.

## Expected serial output

```text
I (xxx) battlestation: Battlestation bring-up starting
I (xxx) battlestation: internal heap=... PSRAM free=...
I (xxx) audio_probe: ES7210 capture: 16000 Hz, 2 ch, 16-bit, PGA 24 dB
I (xxx) wifi_probe: STA started for SSID '...'
I (xxx) ui_probe: UI ready
I (xxx) battlestation: bring-up ready: display + touch + LVGL + ES7210 + hosted Wi-Fi
I (xxx) wifi_probe: connected, IP 192.168.x.x
```

## Acceptance checks

- [ ] Display shows the dark dashboard at the correct orientation.
- [ ] Touching the panel updates `x/y` and draws a cyan dot.
- [ ] Both mic bars move when you speak/tap near the board.
- [ ] `AVG` and `PEAK` read roughly −60…−10 dBFS in a quiet room / speech.
- [ ] Wi-Fi reaches `connected` and shows a valid IPv4 address.
- [ ] FPS is stable (the panel is 60 Hz; expect a high value for this mostly
      static screen; the number is a sanity check, not a benchmark).
- [ ] While touching continuously, FPS rises toward 60; idle ~20 is expected
      because the labels update at 20 Hz.
- [x] Combined Wi-Fi + ping + touch + audio soak: 341 s, zero UI stalls, no reset.
- [ ] Ten-minute soak with no watchdog reboot or display corruption (next milestone).

## Troubleshooting

| Symptom | Likely cause / action |
| --- | --- |
| No `/dev/cu.*` appears | Bad cable, charge-only cable, or missing CH34x driver. Try the other USB-C. |
| `Failed to write to target RAM` | Built-in CDC driver issue on macOS; install WCH CH34x driver 2.1 or use native USB-C. |
| Bootloader rejects chip revision | Build the rev1.3 overlay or the rev3.x image to match `esptool chip_id`. |
| Screen stays black | Check backlight/panel init logs; run `08_lvgl_display_panel` first. |
| Touch coordinates wrong | The BSP uses `mirror_x=1, mirror_y=1`. Compare with `08`; do not mix rotation and touch flags blindly. |
| `ES7210 init failed` | Check I2C 0x40 and the shared I2S bus; run `06_i2s_codec` echo mode. |
| Mic bars flat | Increase PGA gain in menuconfig; verify the two mics are MIC1/MIC2, not the AEC reference. |
| Wi-Fi `esp_wifi_init failed` | The C6 firmware does not match ESP-Hosted 1.4.x. Do not reflash the C6 blindly; capture the host log first. |
| Wi-Fi cannot see AP | AP is 5 GHz, or SSID/password wrong; C6 is 2.4 GHz only. |

## Files

- `main/main.c` — display bring-up and task start
- `main/ui_probe.c` — LVGL screen, touch overlay, FPS
- `main/audio_probe.c` — ES7210 16 kHz stereo capture, RMS/dBFS
- `main/wifi_probe.c` — hosted Wi-Fi station and event state
- `main/diag_probe.c` — heartbeat, LVGL task state, heap, temperature
- `components/esp_lvgl_adapter/` — 0.7.5 local override (DSI deadlock fix)
- `partitions.csv` — 8 MB factory app, no filesystem, no OTA slots
