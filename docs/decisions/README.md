# Decisions

Architecture decision records (ADRs) capture decisions that affect the firmware,
the dependency policy or the portability of the project. Update the record when
a decision changes; do not silently rewrite history.

## Index

| ADR | Decision | Status |
| --- | --- | --- |
| [0001](0001-single-firmware-static-apps.md) | One firmware, static app table, shared services | Accepted |
| [0002](0002-display-pipeline.md) | `DOUBLE_DIRECT` at 180°, PPA off | Accepted |
| [0003](0003-local-adapter-override.md) | Keep a local 0.7.5 adapter override | Accepted, temporary |
| [0004](0004-external-dependencies.md) | Do not vendor upstream dependencies | Accepted |
| [0005](0005-revision-profiles.md) | Keep rev1.x and rev3.x build profiles | Accepted |
| [0006](0006-no-c6-reflash.md) | Do not reflash the C6 without a diagnosis | Accepted |
| [0007](0007-portability-boundary.md) | Hardware boundary for future board ports | Accepted |
| [0008](0008-audio-baseline.md) | 16 kHz stereo 16-bit, 24 dB PGA, dBFS only | Accepted |
| [0009](0009-no-filesystem-or-ota.md) | No filesystem or OTA in the bring-up firmware | Accepted |
| [0010](0010-software-stack.md) | ESP-IDF 5.5.5 + Waveshare BSP 3.0.1 + LVGL 9.5 | Accepted |
| [0011](0011-deferred-app-switching.md) | Defer app switches out of LVGL event callbacks | Accepted |
| [0012](0012-dashboard-cards.md) | Dashboard card grid with fullscreen detail views | Accepted |
| [0013](0013-audio-output-tuner.md) | Audio output ownership and tuner signal chain | Accepted |
| [0014](0014-audio-analysis-defaults.md) | 16 half-octave bands, 20 ms FFT hop, YIN tuner | Accepted |

## Format

Each record has:

- **Status** — proposed, accepted, superseded or rejected.
- **Context** — the forces that required a decision.
- **Decision** — what we chose.
- **Consequences** — what becomes easier or harder, and the removal/revisit
  condition.

Keep records short. Link to the detailed document instead of duplicating
measurements.
