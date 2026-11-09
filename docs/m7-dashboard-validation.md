# M7a/M7b dashboard validation

Scope: dashboard card shell, clock/status, four light cards, detail views,
SNTP, HA WebSocket/REST transport and weather.

## Implemented

- `apps/dashboard.c` — 1024×600 card grid:
  - clock, weather, status across the top;
  - four light cards with on/off and brightness;
  - tapping a card opens a fullscreen detail view; BACK returns.
- `apps/diagnostics.c` — the old probe screen is now the Diagnostics app,
  reachable from the status card/detail.
- `services/time_service.c` — SNTP (pool.ntp.org), Kconfig timezone, waits for
  Wi-Fi connected; reports “not synced” rather than 1970.
- `services/ha_client.c` — WebSocket state push + REST initial sync/commands:
  - four light entities and one weather entity from Kconfig;
  - auth token never logged; HTTP_CLIENT debug logging pinned to INFO;
  - command queue (depth 8, non-blocking) and a worker task;
  - 30 s REST resync, WS auto-reconnect;
  - slider commands are sent once on release, not per drag tick.
- Kconfig: timezone, HA URL/token, four light entity IDs, weather entity,
  optional HA command self-test.

## Build

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3 | 1,319,120 B | 84% |
| rev1.3 + Wi-Fi | 1,409,648 B | 83% |
| rev1.3 + Wi-Fi + HA test | 1,698,672 B | 80% |
| configured HA (dummy) | 1,628,016 B | 81% |

Zero compiler warnings. `./tools/ci.sh --quick` passes (host tests 3/3).
`esp_websocket_client` 1.8.0 is pinned in `dependencies.lock`.

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

### Mock Home Assistant

A local REST + WebSocket mock (`aiohttp`, port 8123) was used with four lights
and one weather entity.

Observed on the board:

```text
ha_client: WebSocket connected
ha_client: light 0: on 70%
ha_client: WebSocket authenticated
ha_client: light 1: off 0%
ha_client: light 2: off 0%
ha_client: light 3: off 0%
ha_client: weather: sunny 22.5 C
ha_selftest: light 0 -> on 40%
ha_client: light 0: on 40%
ha_client: light 0: on 70%      (mock WebSocket push)
ha_selftest: light 0 -> off
ha_client: light 0: off 70%
```

The mock server also logged:

```text
CMD turn_on light.desk {'entity_id': 'light.desk', 'brightness_pct': 40}
CMD turn_off light.desk {'entity_id': 'light.desk'}
WS auth_ok
WS subscribed
```

This verifies:

- WebSocket auth + `subscribe_events`;
- initial REST state sync for lights and weather;
- REST light commands (on/off + brightness);
- WebSocket `state_changed` push for lights and weather;
- reconnect after an initial WS error/disconnect.

## Pending

- Visual check of the card layout, detail views and light controls.
- Real HA instance and token; the mock used `light.desk`/`weather.home`.
- 30-minute live-HA/Wi-Fi soak; only short mock runs were done.
- TLS (`https://`/`wss://`) and mDNS are not enabled; use a LAN HTTP URL/IP in
  M7b.

## Known limitations

- `dashboard.c` still uses the BSP display lock/size macros directly, like the
  old probe. Moving that behind `display_service` is a portability cleanup, not
  an M7 blocker.
- HA WebSocket frames are reassembled in a 4 KB buffer; unusually large
  weather/attribute payloads can be dropped. REST resync every 30 s recovers.
- REST requests use a 4 s timeout and one worker; a slow HA can delay queued
  commands. Commands are best-effort and corrected by the next state push/sync.
- The HA token is a compile-time Kconfig value until M10 moves it to NVS.
