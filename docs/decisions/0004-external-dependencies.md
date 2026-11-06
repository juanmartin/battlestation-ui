# ADR-0004 — Do not vendor upstream dependencies

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The project depends on ESP-IDF, a board BSP, LVGL, an LVGL adapter, audio codec
drivers, ESP-Hosted and several smaller components. Earlier work had extracted
copies of the BSP, adapter and codec into sibling folders for reading. Keeping
those copies inside the repository would:

- make the project depend on local paths rather than declared versions;
- bloat the repository and obscure provenance;
- make a future board port carry unrelated code;
- risk mixing upstream patches with first-party work.

## Decision

Do **not** vendor upstream projects. Instead:

1. Declare dependencies in `main/idf_component.yml`.
2. Commit `dependencies.lock` as the reproducibility anchor.
3. Record provenance, pinned commits and reasons in
   [`../external/upstream.md`](../external/upstream.md).
4. Use `tools/fetch_upstream.sh` to materialize reference sources into a
   gitignored `external/` cache when inspection is needed.
5. Keep only `components/esp_lvgl_adapter/` in-tree, as the documented
   build-critical exception ([ADR-0003](0003-local-adapter-override.md)).
6. Keep datasheets/schematics outside the repository; record hashes and source
   links in [`../reference/`](../reference/).

## Consequences

- The repository stays small and portable; a fresh clone resolves dependencies
  from the registry.
- Upstream source is not available offline until the fetch script runs.
- Version changes require updating the manifest, rebuilding and committing the
  lock file.
- Reference material must be managed separately from the code.

See [`../external/upstream.md`](../external/upstream.md).
