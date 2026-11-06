# Patches

Patches kept for provenance and reuse. **None are applied by default.**

## `idf/0001-ppa-bypass-mb-order.patch`

- **Origin:** bundled with `espressif/esp_lvgl_adapter`, authored by Espressif
  (`lvhaiyu@espressif.com`).
- **Target:** ESP-IDF `components/esp_driver_ppa/src/ppa_srm.c` — **not** the
  adapter.
- **Effect:** forces `ppa_ll_srm_bypass_mb_order(..., true)` to work around a PPA
  hang during scaled/mirrored transfers.
- **Why kept:** PPA is currently disabled; if it is enabled, this workaround and
  `LV_DRAW_SW_DRAW_UNIT_CNT == 1` are prerequisites.

The upstream copy remains inside
`components/esp_lvgl_adapter/0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch`
because that is how the component ships. This copy makes the provenance visible
at the repository level.

Apply only against an ESP-IDF v5.5.5 tree:

```bash
cd "$HOME/esp/esp-idf-v5.5.5"
git apply /path/to/battlestation-ui/patches/idf/0001-ppa-bypass-mb-order.patch
```

Then rebuild the firmware and validate. See
[`../docs/stability.md`](../docs/stability.md).
