# Bring-up

How to build, flash and validate the firmware on macOS. The full narrative,
including the original investigation, is in
[`research/handoff-2026-11-06.md`](research/handoff-2026-11-06.md).

## Prerequisites

- macOS on Apple Silicon (verified on macOS 26.7.1 arm64).
- Xcode command-line tools.
- ESP-IDF **v5.5.5** at `$HOME/esp/esp-idf-v5.5.5`.
- Board connected through USB-C **“USB TO UART”** (CH343P).
- 2.4 GHz Wi-Fi access point; the C6 does not support 5 GHz.

Install ESP-IDF:

```bash
mkdir -p "$HOME/esp"
cd "$HOME/esp"
git clone -b v5.5.5 --depth 1 --recursive \
  https://github.com/espressif/esp-idf.git esp-idf-v5.5.5
cd esp-idf-v5.5.5
./install.sh esp32p4
```

### `VIRTUAL_ENV` trap

If a tool-managed Python environment is active, `install.sh` refuses to create
the IDF environment. Clear it first:

```bash
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
cd "$HOME/esp/esp-idf-v5.5.5"
./install.sh esp32p4
```

Load the environment for every new shell:

```bash
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
source "$HOME/esp/esp-idf-v5.5.5/export.sh"
idf.py --version   # ESP-IDF v5.5.5
```

## Identify the board revision first

```bash
ls /dev/cu.*
esptool.py --chip esp32p4 -p /dev/cu.usbmodemXXXX chip_id
```

Look for `Chip is ESP32-P4 (revision vX.X)`.

- **rev3.x** → default build.
- **rev1.x** → use the rev1.3 overlay below. This unit is rev1.3.

Rev1.x and rev3.x images are not boot-compatible.

## Configure and build

```bash
cd "$HOME/REPOS/REPROPIOS/BATTLESTATION/battlestation-ui"

unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
source "$HOME/esp/esp-idf-v5.5.5/export.sh"

idf.py set-target esp32p4
idf.py menuconfig
```

In `Battlestation bring-up` set:

- Wi-Fi SSID (2.4 GHz only) and password.
- ES7210 PGA gain — start at 24 dB.
- Display rotation — **180** for the product orientation.
- Touch flags are rotation-dependent in `main.c`: 180° uses no mirrors; 0° uses
  mirror x/y.

Also confirm `Component config → ESP32-P4` matches the silicon.

### rev3.x

```bash
idf.py build
```

### rev1.x (this unit)

```bash
idf.py fullclean
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
       -D SDKCONFIG="$PWD/sdkconfig.rev1_3" build
```

### Wi-Fi credentials without committing them

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
```

> Never commit the real `sdkconfig.wifi.local`. The repository keeps only the
> example file.

## Flash and monitor

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Exit with `Ctrl+]`.

If auto-reset fails: hold **BOOT**, press/release **RESET**, release **BOOT**,
then flash again.

If flashing fails with `Failed to write to target RAM`, use the native USB-C
port or install the WCH CH34x driver. This unit did not need it.

## First-run validation order

1. **`00_board_check`** — revision, 32 MB flash, PSRAM, heap.
2. **`08_lvgl_display_panel`** — panel, backlight, GT911, touch mapping.
3. **`06_i2s_codec`** — ES7210/ES8311/I2S/PA in echo mode.
4. **`05_wifistation`** — C6 ESP-Hosted handshake and IP.
5. **`battlestation-ui`** — all paths live simultaneously.

## Acceptance checks

- [ ] Dark dashboard at 1024×600, correct orientation.
- [ ] Touch updates `x/y` and draws a cyan dot; no mirroring.
- [ ] Both mic bars respond; `AVG`/`PEAK` plausible in a quiet room.
- [ ] Wi-Fi reaches `connected` with a valid IPv4 address.
- [ ] FPS stable: idle ~20 fps, continuous touch ~60 fps.
- [ ] No watchdog reset; heap/PSRAM stable.
- [x] 386 s display/audio/touch soak.
- [x] 341 s Wi-Fi + ping + touch + audio soak.
- [ ] 10-minute idle soak (next gate).

## DSI stress probe

The raw panel test lives in [`probes/dsi/`](../probes/dsi/):

```bash
cd probes/dsi
idf.py -B build \
  -D SDKCONFIG="$PWD/sdkconfig.rev1_3" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
  set-target esp32p4 build
idf.py -B build -D SDKCONFIG="$PWD/sdkconfig.rev1_3" \
  -p /dev/cu.usbmodemXXXX flash monitor
```

Expected: `frame=30 fps=66.1 timeouts=0`.

## M5 app-switch self-test

The M5 launcher has an opt-in hardware self-test that cycles
launcher → dashboard → organism → settings 25 times (100 switches) and logs
LVGL child count and heap after each cycle.

```bash
printf 'CONFIG_APP_SWITCH_SELFTEST=y\n' > "$TMPDIR/selftest.defaults"
idf.py -B build-selftest \
  -D SDKCONFIG="$PWD/sdkconfig.selftest" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults;$TMPDIR/selftest.defaults" \
  set-target esp32p4 build
idf.py -B build-selftest -D SDKCONFIG="$PWD/sdkconfig.selftest" \
  -p /dev/cu.usbmodemXXXX flash monitor
```

Expected tail:

```text
app_manager: selftest done: switches=100 children=6 baseline=6 child_delta=0 ...
```

Leave `CONFIG_APP_SWITCH_SELFTEST` disabled for normal images.

## Build notes

- Do not run several first-time `idf.py` builds in parallel. They share the
  Component Manager cache and can corrupt it. Build new projects serially.
- `idf.py build` does not touch the board; `idf.py flash` resets it.
- The first build downloads the BSP, LVGL and ESP-Hosted.

## Troubleshooting

| Symptom | Likely cause / action |
| --- | --- |
| No `/dev/cu.*` | Bad/charge-only cable, or try the other USB-C |
| `Failed to write to target RAM` | macOS CDC issue; use native USB-C or WCH driver |
| Bootloader rejects chip revision | Build the matching rev1.3 or rev3.x profile |
| Screen stays black | Check backlight/panel logs; run `08` first |
| Touch coordinates wrong | Do not mix rotation and mirror flags; see hardware doc |
| `ES7210 init failed` | Check I2C `0x40` and shared I2S; run `06` |
| Mic bars flat | Raise PGA; verify MIC1/MIC2, not AEC reference |
| Wi-Fi `esp_wifi_init failed` | C6/host protocol mismatch; capture log before reflashing |
| Wi-Fi cannot see AP | AP is 5 GHz, or wrong credentials |
| UI freezes after ~20 s | Confirm `DOUBLE_DIRECT` + adapter 0.7.5; see stability doc |
