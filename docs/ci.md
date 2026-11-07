# CI

Local CI is the default for this project. It runs the same host tests and the
same rev1.3 firmware build on the machine that has the ESP-IDF toolchain and the
Component Manager cache. No hosted runner, billing account or network service is
required.

## Commands

```bash
make ci          # whitespace + host tests + rev1.3 firmware build
make ci-quick    # whitespace + host tests only
make host        # host tests only
make firmware    # rev1.3 firmware build only
make asan        # host tests under ASan/UBSan
./tools/ci.sh --wifi   # also build the Wi-Fi variant
make clean       # remove local CI build dirs/configs
```

The script lives at [`../tools/ci.sh`](../tools/ci.sh). It:

1. Runs `git diff --check` on code/build files (Markdown line-break spaces are
   intentional).
2. Builds and runs `tests/host` (FFT, features, mixer) with C11 and `-Werror`.
3. Builds the rev1.3 firmware into `build-ci/` with zero warnings.
4. Optionally repeats the firmware build with the Wi-Fi stack and placeholder
   credentials (`--wifi`).
5. Optionally runs the host suite with ASan/UBSan (`--asan`).

It does **not** flash or touch the board. Hardware validation remains a separate
gate.

## Git hooks

The repository ships optional hooks:

- `pre-commit` → `tools/ci.sh --quick`
- `pre-push` → `tools/ci.sh`

Install once per clone:

```bash
tools/install_git_hooks.sh
```

Bypass with `git commit --no-verify` or `git push --no-verify` when needed.

## Environment

- ESP-IDF **v5.5.5** at `~/esp/esp-idf-v5.5.5`, or set `IDF_PATH`.
- Host tests need only CMake, a C11 compiler and `libm`.
- The first firmware build populates the Component Manager cache; later runs are
  incremental.

## Why not hosted CI?

Hosted CI is possible, but a full ESP-IDF build needs the exact toolchain and a
large component cache, and it cannot exercise the panel, touch, codec or C6. For
this project the useful automated gate is local; hardware soaks stay manual.

If a hosted service is required later:

| Option | Fit |
| --- | --- |
| **Self-hosted runner on this Mac** | Best hosted option; keeps the toolchain/cache local, but still needs the CI service account |
| **Buildkite** | Good for a self-hosted agent; free tier for small use |
| **CircleCI / GitLab CI** | Workable with an ESP-IDF Docker image; slower and cache-heavy |
| **Woodpecker / Drone** | Self-hosted, lightweight; needs a server |
| **GitHub Actions** | Currently blocked by an account billing issue; re-enable if the account is fixed |

For host-only tests, almost any CI works; the firmware build is the expensive
part. Keep the local `make ci` as the source of truth even if a hosted service is
added later.
