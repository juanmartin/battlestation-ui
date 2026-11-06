# Hardware

Verified against the official schematic, the Waveshare BSP 3.0.1 source and the
physical unit (2026-11-06). The BSP is the authoritative pin source; do not infer
pins from a generic ESP32-P4 board.

## Board

| Subsystem | Value |
| --- | --- |
| Board | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (SKU 32510; 7B-C camera variant 32511) |
| Main SoC | ESP32-P4NRW32X, dual-core HP RISC-V up to 360 MHz + LP core |
| PSRAM | 32 MB in-package, 200 MHz, X16 |
| Flash | 32 MB external NOR (GD, QIO on this unit) |
| Wireless | ESP32-C6-MINI-1-N4 (4 MB flash), SDIO to P4; Wi-Fi 6 2.4 GHz + BLE 5 |
| Display | 7-inch IPS, 1024×600, MIPI-DSI, EK79007 |
| Touch | GT911 capacitive, I2C, `0x5D` (or `0x14`), polling mode |
| Audio ADC | ES7210 4-channel, I2C `0x40` |
| Audio DAC | ES8311, I2C `0x18`, NS4150B class-D speaker amp |
| Microphones | Two onboard mics on ES7210 MIC1/MIC2; MIC3/MIC4 are AEC reference |
| microSD | SDIO 3.0, slot 0, GPIO39–44 (not used) |
| Power | 5 V via either USB-C, onboard switch |

## Pin map

| Function | P4 pin | Notes |
| --- | --- | --- |
| I2C SCL | GPIO8 | Shared by GT911, ES8311, ES7210 |
| I2C SDA | GPIO7 | Shared |
| I2S MCLK | GPIO13 | To both codecs |
| I2S SCLK/BCLK | GPIO12 | Shared |
| I2S LRCK/WS | GPIO10 | Shared |
| I2S DOUT (P4 → codec) | GPIO9 | ES8311 DAC |
| I2S DIN (codec → P4) | GPIO11 | ES7210 ADC |
| Speaker PA enable | GPIO53 | NS4150B, active high |
| LCD backlight | GPIO32 | LEDC PWM, output inverted in BSP |
| LCD reset | GPIO33 | Shared panel reset |
| Touch reset | NC | GT911 reset not wired |
| Touch interrupt | NC | Polling only |
| microSD CLK/CMD/D0–D3 | GPIO43/44/39–42 | Not used |
| UART0 TX/RX | GPIO37/38 | CH343P |
| C6 SDIO D0–D3 | GPIO14–17 | 4-bit bus |
| C6 SDIO CLK | GPIO18 | |
| C6 SDIO CMD | GPIO19 | |
| C6 reset | GPIO54 | `C6_CHIP_PU`, 0 Ω link |
| Native USB | `USB1P1_N/P` (GPIO24/25) | USB Serial/JTAG, FS |

## Display

- EK79007, MIPI-DSI, 1024×600, 2 data lanes at 1000 Mbit/s each.
- DPI timing: HSYNC 1344, HBP 160, HFP 160; VSYNC 635, VBP 23, VFP 12;
  52 MHz pixel clock.
- RGB565 default. RGB888 is possible but costs 50% more memory and bandwidth;
  stay RGB565.
- Backlight: LEDC channel 1, 5 kHz, 10-bit; `bsp_display_brightness_set(0…100)`.
- Default BSP tearing mode is `TRIPLE_PARTIAL` with 3 DPI framebuffers. This
  project uses `DOUBLE_DIRECT`; see [`stability.md`](stability.md).

### Touch orientation quirk

The factory/BSP product orientation is **180°**, but on this unit the correct
GT911 mapping at 180° is:

```text
swap_xy = 0, mirror_x = 0, mirror_y = 0
```

The official `09`/factory combination of 180° + `mirror_x=1, mirror_y=1` maps
the visual top-left to `(1023,599)` — a 180° touch error. At rotation 0°,
`mirror_x=1, mirror_y=1` is correct. Do not copy the BSP touch flags blindly.

GT911 and `CONFIG_ESP_LCD_TOUCH_MAX_POINTS=5` support five points, but the
adapter's default config registers one pointer. The bring-up intentionally keeps
single-pointer semantics.

## Audio

- ES7210 is a 4-channel ADC, but the board uses:
  - **MIC1/MIC2** — the two onboard microphones (differential).
  - **MIC3/MIC4** — fed from the speaker output network, labeled `AEC ADC`;
    they are not additional microphones.
- `esp_codec_dev` defaults to `MIC1 | MIC2`, giving stereo capture.
- ES8311 is the speaker DAC; GPIO53 enables the NS4150B amplifier.
- Both codecs share one duplex I2S peripheral. Opening speaker and microphone
  with different formats is rejected; the audio service must own one format.
- Baseline: 16 kHz, 16-bit, stereo, 24 dB PGA. ES7210/ES8311 support up to
  96 kHz; 48 kHz is optional later.
- dBFS is computed from 16-bit PCM and is **not calibrated dB SPL**. PGA gain
  changes the reading; the board has no per-unit sensitivity data.

## Wireless (C6)

- The P4 has no radio. The C6 is a slave over **SDIO**.
- Host runs TCP/IP (`esp_netif`/lwIP); the C6 runs the Wi-Fi/BT stack.
  `esp_wifi_remote` exposes the normal `esp_wifi_*` API; `esp_hosted` implements
  the SDIO/RPC transport.
- Pinned pair: `esp_hosted 1.4.7` + `esp_wifi_remote 1.2.5`, matching the
  factory C6 image.
- Resolved SDIO config: CLK 18, CMD 19, D0–D3 14–17, reset 54, 4-bit, 40 MHz.
- C6 firmware is a separate image. Do not reflash it without a protocol
  mismatch diagnosis and a recovery plan; see
  [ADR-0006](decisions/0006-no-c6-reflash.md).
- 2.4 GHz only. BLE is possible but not yet validated.

Host init sequence:

```text
nvs_flash_init
  → esp_netif_init
  → esp_event_loop_create_default
  → esp_netif_create_default_wifi_sta
  → esp_wifi_init
       └─ esp_wifi_remote → esp_hosted → SDIO 4-bit @ 40 MHz → C6 reset
  → esp_wifi_set_mode(STA)
  → esp_wifi_set_config(SSID/password)
  → esp_wifi_start
       └─ WIFI_EVENT_STA_START → esp_wifi_connect
  → IP_EVENT_STA_GOT_IP
```

## USB ports

| Port | Role | Can flash P4? | Use |
| --- | --- | --- | --- |
| **USB-C “USB TO UART”** | CH343P → UART0 GPIO37/38, auto-reset | **Yes** | Primary flashing + monitor |
| **USB-C “USB1.1”** | P4 native USB 2.0 FS / USB Serial-JTAG | **Yes** | Driverless fallback |
| **USB-A “USB OTG”** | USB 2.0 HS **host** | **No** | Keyboard/mouse/storage |
| **C6 UART header** | C6 U0TXD/U0RXD, IO9, CHIP_PU | C6 only | Deliberate C6 recovery only |

On this Mac the CH343P appears as `/dev/cu.usbmodem5B901660791` with Apple's
built-in CDC driver. Flashing at 460800 baud works; the WCH driver is not
required. Always use `/dev/cu.*`, not `/dev/tty.*`.

## Revision traps

Two independent axes:

1. **P4 silicon** — repository default is rev3.x, but this unit is **rev1.3**.
   Rev1.x and rev3.x images are not boot-compatible. Build with
   `sdkconfig.rev1_3.defaults` on this unit.
2. **PCB revision** — BSP version numbers are not PCB revisions. The BSP README
   saying “HW V1.0 → BSP ^1” does not contradict BSP 3.0.1. The 7B-C camera
   variant adds an OV5647 but does not change display/touch/audio pins.

## Documentation inconsistencies found

| Claim | What the code/schematic shows | Decision |
| --- | --- | --- |
| “ESP-IDF V5.3.1 or later” | BSP requires ≥5.5; adapter requires ≥5.5.5 | Use v5.5.5 |
| “v5.5.1–v5.5.4 recommended” | v5.5.5 is current and CI-validated | Use v5.5.5 |
| `09_lvgl_demo_v9` README says LVGL 9.2.x | Manifest pins 9.5.0 | Use 9.5.0 |
| `00_board_check` README says 16 MB flash | Hardware/runtime say 32 MB | Trust runtime |
| BSP README says `esp_codec_dev 1.2.*` | BSP 3.0.1 resolves 1.5.11 | Trust the manifest |

## Portability note

Everything above is board-specific. The application layer should not include BSP
headers or call these pins directly; see
[`architecture.md`](architecture.md) and
[ADR-0007](decisions/0007-portability-boundary.md).
