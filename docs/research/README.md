# Research snapshot

`handoff-2026-11-06.md` is the original research and development handoff,
preserved unchanged as source material. It contains the full investigation,
measurements, repository analysis, setup transcript and initial roadmap.

Living documents derive from it and should be updated instead:

- [`../status.md`](../status.md)
- [`../hardware.md`](../hardware.md)
- [`../bringup.md`](../bringup.md)
- [`../stability.md`](../stability.md)
- [`../architecture.md`](../architecture.md)
- [`../roadmap.md`](../roadmap.md)
- [`../decisions/`](../decisions/)

The snapshot is not authoritative for current status. Where it conflicts with a
living document, the living document and the ADRs win.

Unit identifiers (MAC address, LAN IP, USB serial port) are redacted in this
public copy; the technical content is unchanged.

Known differences after the bring-up:

- The snapshot's status line predates runtime validation; the board is now
  verified as rev1.3 and all five paths run concurrently.
- It describes the adapter 0.6.4 baseline; the project now carries 0.7.5 and
  `DOUBLE_DIRECT`.
- The repository layout has been reorganized (docs, probes, tools); paths inside
  the snapshot refer to the original workspace.
