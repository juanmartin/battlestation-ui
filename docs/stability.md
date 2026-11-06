# Display stability

The board does not crash. The failure observed during bring-up was a
**display-pipeline deadlock**: the LVGL worker stopped while audio, heap, PSRAM
and temperature stayed healthy.

## Failure signature

```text
heartbeat frozen
lvgl task = blocked
audio task = alive
internal heap ~215 KB free
PSRAM ~28.9 MB free
temperature 35–38 C
no panic, no WDT reset, no brownout, no DSI underrun log
```

Audio logs continued after the UI stopped, ruling out a CPU crash, power
brownout, thermal event and memory leak. The backlight fading is a secondary
effect of the panel no longer being refreshed.

## Root cause

`esp_lvgl_adapter` 0.6.4 has an unbounded framebuffer wait in the rotated
MIPI-DSI flush path:

```text
LVGL worker holds the recursive LVGL mutex
  → lv_timer_handler()
    → display_bridge_v9_flush_partial_rotate() / flush_direct_rotate()
      → display_bridge_pipeline_wait_free_buf()
        → ulTaskNotifyTake(pdTRUE, portMAX_DELAY)
```

Espressif issue [#793](https://github.com/espressif/esp-iot-solution/issues/793)
documents a submit/completion race on this path (P4 v1.3, adapter 0.6.4/0.7.2),
fixed in **0.7.3**. Issue
[#804](https://github.com/espressif/esp-iot-solution/issues/804) covers the
remaining unbounded-recovery case if DSI completion genuinely stops.

Upgrading to 0.7.5 fixes the #793 race but **does not fix this unit's
partial-rotate deadlock**; a different loss of progress remains in that
pipeline.

## Isolation measurements

| Test | Configuration | Result |
| --- | --- | --- |
| Raw `esp_lcd` stress | no LVGL/touch/audio/Wi-Fi, 60% backlight | **70 s stable, 66 fps, 0 completion timeouts** |
| LVGL probe | rotation 0°, `TRIPLE_PARTIAL`, audio/touch | **75 s stable** |
| LVGL probe | rotation 180°, `TRIPLE_PARTIAL`, adapter 0.6.4 | **deadlock ~26 s** |
| LVGL probe | rotation 180°, `TRIPLE_PARTIAL`, adapter 0.7.5 | **deadlock ~21–26 s** |
| LVGL probe | rotation 180°, `TRIPLE_FULL`, adapter 0.7.5 | **75 s stable with touch** |
| LVGL probe | rotation 180°, `DOUBLE_DIRECT`, adapter 0.7.5 | **386 s stable with touch/audio/LVGL, ~20 fps** |
| LVGL + Wi-Fi + ping | 180°, `DOUBLE_DIRECT`, 5 Hz ping, touch | **341 s stable, 0 stalls; 1.6% loss, 30.9 ms avg** |

Conclusions:

1. Panel, DSI PHY, PSRAM, backlight and power path are healthy.
2. The failure is specific to the adapter's **rotated partial-refresh** path,
   not to 180° rotation in general.
3. Adapter 0.7.5 alone does not fix this unit's partial-rotate deadlock.
4. `DOUBLE_DIRECT + 180°` with 0.7.5 is the current stable default.
5. `TRIPLE_FULL + 180°` is a slower fallback.
6. The factory firmware also uses `DOUBLE_DIRECT + 180°` with an older adapter;
   it shares the same unbounded wait, consistent with the factory freeze.
7. The 341 s combined soak had zero UI stalls and no reset; network latency is
   acceptable for HA but remains a tuning item.

## Current configuration

```text
tear mode:  ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT
rotation:   180°
adapter:    esp_lvgl_adapter 0.7.5 (local override)
PPA:        disabled
framebuffers: 2 (DOUBLE_DIRECT)
```

`TRIPLE_PARTIAL` remains available as a fast mode but is unsafe on this unit.
`TRIPLE_FULL` is the slower fallback.

See [ADR-0002](decisions/0002-display-pipeline.md) for the decision record.

## Local adapter override

The BSP 3.0.1 manifest resolves `espressif/esp_lvgl_adapter 0.6.4`. This project
carries 0.7.5 under `components/esp_lvgl_adapter/` and points at it with
`override_path` in `main/idf_component.yml`.

Do not remove the override until Waveshare widens the BSP dependency from
`~0.6`. This is the only vendored dependency in the repository; details and the
removal condition are in
[ADR-0003](decisions/0003-local-adapter-override.md).

## PPA patch caveat

The adapter ships
`0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch`, which patches
**ESP-IDF's `esp_driver_ppa`**, not the adapter itself. It forces
`ppa_ll_srm_bypass_mb_order(..., true)` to work around a PPA hang.

It is **not applied** in this project because PPA is disabled. If PPA is enabled
later:

1. Apply the patch to the ESP-IDF tree
   (copy kept at `patches/idf/0001-ppa-bypass-mb-order.patch`).
2. Set `LV_DRAW_SW_DRAW_UNIT_CNT == 1`.
3. Benchmark and soak `DOUBLE_DIRECT` and the candidate mode before committing.

## Power note

macOS allocates only 138 mA to the CH343P, and a 7-inch panel at 100% backlight
can draw more. A powered hub is good practice for headroom, but power is **not**
the root cause: raw DSI was stable at 60% backlight, and the factory firmware
also froze on a 2.4 A charger.

## Recurrence detection

`diag_probe` prints a 5-second heartbeat with `fps`, LVGL task state, heap and
temperature. A recurrence is visible as:

```text
heartbeat frozen
lvgl task = blocked
audio task = alive
```

Do not interpret a blank panel during `idf.py flash` as a freeze; flashing
resets the board and the dashboard reappears.
