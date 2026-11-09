# M7a dashboard validation

Scope: dashboard card shell, clock/status, four light cards, detail views,
SNTP and the HA client stub. HA transport and weather data are M7b.

## Implemented

- `apps/dashboard.c` — 1024×600 card grid:
  - clock, weather, status across the top;
  - four light cards with on/off and brightness;
  - tapping a card opens a fullscreen detail view; BACK returns.
- `apps/diagnostics.c` — the old probe screen is now the Diagnostics app,
  reachable from the status card/detail.
- `services/time_service.c` — SNTP (pool.ntp.org), Kconfig timezone, waits for
  Wi-Fi connected; reports “not synced” rather than 1970.
- `services/ha_client.c` — M7a stub:
  - unconfigured → offline, no fake light/weather data;
  - configured → connecting, no transport yet;
  - `set_light` returns invalid-state/not-supported until M7b.
- Kconfig: timezone, HA URL/token, four light entity IDs, weather entity.
- Dashboard light controls keep a local preview while HA is offline and show
  that state explicitly.

## Build

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3 | 1,319,120 B | 84% |
| rev1.3 + Wi-Fi | 1,408,752 B | 83% |
| rev1.3 + Wi-Fi + switch self-test | 1,409,824 B | 83% |

Zero compiler warnings. `./tools/ci.sh --quick` passes (host tests 3/3).

## Hardware

### 100-switch self-test with dashboard

- 100 switches; dashboard mounted/unmounted 25 times.
- Launcher child count 6 → 6, PSRAM delta 0, no errors/stalls.
- Internal heap delta −7,884 B one-time (dashboard/LVGL/font caches), then
  stable around 239–243 KB.
- LVGL stack high-water 1,916 B free (above the 1 KB reserve).
- SNTP started after Wi-Fi connected; HA stub logged offline as expected.

### Device smoke

30 s normal Wi-Fi build with `tools/device_test.py`:

```text
boot/launcher: yes
audio capture: yes
audio analysis: yes
heartbeat: 99 -> 497
max stall: 0
wifi: connected
errors/panic: 0/no
result: PASS
```

## Pending

- Visual check of the card layout, detail views and light controls.
- M7b: REST/WebSocket HA transport, four real light states/commands, weather.
- M7b: pending/optimistic command state and command coalescing (sliders
  currently enqueue on every change).
- Long soak with live HA.

## Known limitation

`dashboard.c` still uses the BSP display lock/size macros directly, like the old
probe. Moving that behind `display_service` is a portability cleanup, not an M7
blocker.
