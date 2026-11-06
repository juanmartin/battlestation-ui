# Upstream dependencies

## Policy

Upstream projects are **not vendored**. The repository declares dependencies in
`main/idf_component.yml`, commits `dependencies.lock`, and records provenance
here. `tools/fetch_upstream.sh` can materialize pinned sources into a gitignored
`external/` cache for reading or debugging.

The only in-tree exception is `components/esp_lvgl_adapter/`; see
[ADR-0003](../decisions/0003-local-adapter-override.md).

## Resolved build dependencies

From `dependencies.lock` (2026-11-06). Update this table when the lock changes.

| Component | Version | Role | Source |
| --- | --- | --- | --- |
| ESP-IDF | 5.5.5 | Framework/toolchain | <https://github.com/espressif/esp-idf> |
| `waveshare/esp32_p4_wifi6_touch_lcd_7b` | 3.0.1 | Board BSP: display, touch, audio, adapter | [BSP](https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_7b/versions/3.0.1) |
| `lvgl/lvgl` | 9.5.0 | UI toolkit | <https://github.com/lvgl/lvgl> |
| `espressif/esp_lvgl_adapter` | 0.7.5 | Local override: display/touch/tearing | [Component](https://components.espressif.com/components/espressif/esp_lvgl_adapter) |
| `espressif/esp_codec_dev` | 1.5.11 | ES7210/ES8311 drivers | [Component](https://components.espressif.com/components/espressif/esp_codec_dev) |
| `espressif/esp_hosted` | 1.4.7 | C6 SDIO/RPC transport | <https://github.com/espressif/esp-hosted-mcu> |
| `espressif/esp_wifi_remote` | 1.2.5 | `esp_wifi_*` API over ESP-Hosted | Component registry |
| `espressif/esp_lcd_ek79007` | 2.0.2~1 | EK79007 DSI panel driver | Component registry |
| `espressif/esp_lcd_touch_gt911` | 1.2.1 | GT911 touch driver | Component registry |

## Pinned reference sources (not built)

These are for inspection only. The fetch script pins them by commit.

| Source | Ref | Use |
| --- | --- | --- |
| Waveshare board repository | `9fcd49a8c927a00f5583c22d568b3448addbe51c` | Examples, factory firmware, docs, BSP source |
| Waveshare BSP 3.0.1 | `d7567970be6d6a94077727eb69d580d63a898eab` | Pin/display/audio source of truth |
| `espressif/esp-iot-solution` — `components/display/tools/esp_lvgl_adapter` | `5fbb2597b88f21ec41e952f81292ccd6ae569761` | Adapter 0.7.5 source |
| `espressif/esp-adf` — `components/esp_codec_dev` | `af7b72fb2b73d4f513d3cede01c13518ab216735` | Codec reference snapshot 1.6.2 (build resolves 1.5.11) |
| ESP-IDF | tag `v5.5.5` | Framework; install rather than fetch |

Repository URLs:

- <https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B>
- <https://github.com/waveshareteam/Waveshare-ESP32-components>
- <https://github.com/espressif/esp-iot-solution>
- <https://github.com/espressif/esp-adf>
- <https://github.com/espressif/esp-hosted-mcu>
- <https://github.com/espressif/esp-idf>

## Fetching reference sources

```bash
tools/fetch_upstream.sh
```

The default cache is `./external/` (gitignored). Override it with
`BATTLESTATION_EXTERNAL`:

```bash
BATTLESTATION_EXTERNAL="$HOME/src/battlestation-upstream" tools/fetch_upstream.sh
```

The script uses HTTPS and shallow fetches, and sparse-checkouts only the needed
component paths for the large Espressif repositories. It does not modify the
firmware or the dependency lock.

## Vendored exception

`components/esp_lvgl_adapter/` is 0.7.5 from `esp-iot-solution` commit
`5fbb259…`. It is kept in-tree because BSP 3.0.1 pins `~0.6` and the older line
deadlocks the display. Upstream files should not be edited; local changes must be
recorded as patches under `patches/`.

The adapter bundles
`0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch`, authored by
Espressif. It patches **ESP-IDF's `esp_driver_ppa`**, not the adapter. A copy is
kept at `patches/idf/0001-ppa-bypass-mb-order.patch`; it is not applied because
PPA is disabled.

## Updating a dependency

1. Change the version in `main/idf_component.yml`.
2. Build serially (first builds share the Component Manager cache).
3. Run the relevant acceptance gate and a stability soak.
4. Commit `dependencies.lock` and update the table above with the date.
5. If the change affects the board BSP or adapter, update the matching ADR.

## Reference documents

Datasheets and schematics are **not** committed. See
[`../reference/README.md`](../reference/README.md) for filenames, hashes and
source links.
