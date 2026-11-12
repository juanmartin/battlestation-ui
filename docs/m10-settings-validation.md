# M10 settings and provisioning validation

Target: rev1.3, 1024×600, NVS-backed runtime settings.

## Settings store

- Single NVS namespace `battlestation`, key `cfg`, one versioned blob.
- Blob header: magic `BST1`, version 1, payload size, CRC32.
- Invalid/missing/wrong-version blobs fall back to Kconfig defaults; first boot
  persists the defaults.
- Pure-C codec/validation lives in `engine/settings_codec.c` and is host-tested.
- Persisted fields: brightness, mic gain, output volume, A4 reference, Wi-Fi
  SSID/password, HA URL/token, timezone, four light entity IDs, weather entity.

## Runtime integration

- `main` initializes settings before display/audio/Wi-Fi and applies brightness,
  mic gain, output volume and timezone.
- `audio_service` has runtime mic-gain setter/getter; `time_service` has runtime
  POSIX timezone setter; both are applied live from Settings.
- Wi-Fi uses the NVS credentials; Settings can update them live via
  `wifi_probe_set_credentials()` and scan nearby 2.4 GHz APs.
- HA client loads URL/token/entity IDs from settings at boot. HA changes are
  saved and applied with the Settings **SAVE + RESTART** action.
- Tuner reads the stored A4 reference when it starts.
- The internal ESP32-P4 die temperature is available in Settings → SYSTEM and
  Diagnostics (shared `temperature_service`).

## Settings app

- Four tabs: DISPLAY / AUDIO, NETWORK, HOME ASSISTANT, SYSTEM.
- Display/audio: brightness, mic gain, output volume, A4 sliders; live apply,
  NVS write on release/SAVE.
- Network: live state/IP/RSSI, SCAN, up to 8 APs, tap-to-fill SSID, masked
  password, CONNECT.
- Home Assistant: URL, masked token, four light IDs, weather entity; URL
  validation; SAVE and SAVE + RESTART (armed twice).
- System: 12 human-readable timezone options, live local-time readout, P4 die
  temperature, reset defaults (armed twice).
- One reusable full-width keyboard editor with masked secret fields; no writes
  while dragging.

## Validation

- Host tests: **8/8**, including `test_settings` roundtrip, validation/clamping,
  CRC/magic/version/size corruption and short buffers.
- Hardware: settings boot source switched `defaults` → `nvs` on the second boot;
  brightness/mic values changed in the UI survived a reset (`brightness=65`,
  `mic=37`). Normal launcher + Wi-Fi smoke: **PASS**, zero errors, zero stalls.
- Editor/keyboard and timezone page layout verified visually after the
  full-width/taller-keyboard fix; paragraphs reflow naturally and the timezone
  note no longer clips.
- P4 die temperature read 39.3 °C in Diagnostics and Settings.

## Builds

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3 + Wi-Fi, boot settings | 1,787,552 B | 79% |
| rev1.3 + Wi-Fi, normal launcher | 1,788,272 B | 79% |

Zero compiler warnings.

## Pending / limits

- Manual SCAN/CONNECT and timezone-tap confirmation from the final UI build.
- Real HA URL/token change + restart; invalid token behavior on hardware.
- HA configuration applies on restart, not live; brightness/audio/timezone/Wi-Fi
  apply live.
- Wi-Fi scan has no service-level timeout/abort if `SCAN_DONE` never arrives.
- Full 30-minute settings/HA soak.
- Volume/A4/timezone use the same persistence path as the verified
  brightness/mic fields.
