#!/usr/bin/env bash
#
# Fetch pinned upstream sources for reference/debugging.
# These checkouts are NOT part of the firmware build and are gitignored.
#
# Usage:
#   tools/fetch_upstream.sh
#   BATTLESTATION_EXTERNAL="$HOME/src/battlestation-upstream" tools/fetch_upstream.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${BATTLESTATION_EXTERNAL:-$ROOT/external}"

log() { printf '==> %s\n' "$*"; }

fetch_ref() {
    local name="$1"
    local url="$2"
    local ref="$3"
    local sparse="${4:-}"
    local dest="$CACHE/$name"

    if [ -d "$dest/.git" ]; then
        log "$name already present at $dest (remove it to refresh)"
        return 0
    fi

    if [ -e "$dest" ]; then
        printf 'error: %s exists but is not a git checkout\n' "$dest" >&2
        return 1
    fi

    log "fetching $name @ ${ref:0:12}"
    mkdir -p "$dest"
    git -C "$dest" init -q
    git -C "$dest" remote add origin "$url"

    if [ -n "$sparse" ]; then
        git -C "$dest" config core.sparseCheckout true
        printf '%s\n' "$sparse" > "$dest/.git/info/sparse-checkout"
    fi

    # GitHub supports fetching a reachable commit by SHA with depth 1.
    git -C "$dest" fetch -q --depth 1 origin "$ref"
    git -C "$dest" checkout -q FETCH_HEAD
    printf '%s\n' "$ref" > "$dest/.fetched-ref"
    log "$name ready"
}

fetch_ref \
    "waveshare-board" \
    "https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B.git" \
    "9fcd49a8c927a00f5583c22d568b3448addbe51c"

fetch_ref \
    "esp-iot-solution" \
    "https://github.com/espressif/esp-iot-solution.git" \
    "5fbb2597b88f21ec41e952f81292ccd6ae569761" \
    "components/display/tools/esp_lvgl_adapter/"

fetch_ref \
    "esp-adf" \
    "https://github.com/espressif/esp-adf.git" \
    "af7b72fb2b73d4f513d3cede01c13518ab216735" \
    "components/esp_codec_dev/"

log "reference sources in $CACHE"
log "ESP-IDF v5.5.5 is installed separately; see docs/bringup.md"
