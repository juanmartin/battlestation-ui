#!/usr/bin/env bash
#
# Local CI for Battlestation.
#
# Default: whitespace check + host DSP tests + rev1.3 firmware build.
# No hardware, no network services, no GitHub Actions required.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

HOST_ONLY=0
FIRMWARE_ONLY=0
ASAN=0
WIFI=0
QUICK=0
DEVICE=0
PORT="${PORT:-}"
SOAK_SECONDS=60

usage() {
    cat <<'EOF'
Usage: tools/ci.sh [options]

  (default)        host tests + rev1.3 firmware build
  --quick          whitespace check + host tests only
  --host-only      host tests only
  --firmware-only  firmware build only
  --asan           also run host tests with ASan/UBSan
  --wifi           also build the Wi-Fi variant with placeholder credentials
  --device         also flash and run a device smoke/soak test
  --port PORT      serial port for --device (or set PORT)
  --soak-seconds N device capture length (default 60)
  -h, --help       show this help

Environment:
  IDF_PATH         ESP-IDF tree (default: $HOME/esp/esp-idf-v5.5.5)
  PORT             serial port for --device
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --host-only) HOST_ONLY=1 ;;
        --firmware-only) FIRMWARE_ONLY=1 ;;
        --asan) ASAN=1 ;;
        --wifi) WIFI=1 ;;
        --device) DEVICE=1 ;;
        --port) PORT="$2"; shift ;;
        --soak-seconds) SOAK_SECONDS="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [ "$QUICK" -eq 1 ]; then
    HOST_ONLY=1
fi
if [ "$HOST_ONLY" -eq 1 ] && [ "$FIRMWARE_ONLY" -eq 1 ]; then
    echo "error: --host-only and --firmware-only are mutually exclusive" >&2
    exit 2
fi
if [ "$DEVICE" -eq 1 ] && [ "$HOST_ONLY" -eq 1 ]; then
    echo "error: --device cannot be combined with --host-only/--quick" >&2
    exit 2
fi
if ! [[ "$SOAK_SECONDS" =~ ^[0-9]+$ ]] || [ "$SOAK_SECONDS" -lt 5 ]; then
    echo "error: --soak-seconds must be an integer >= 5" >&2
    exit 2
fi

log() { printf '\n==> %s\n' "$*"; }

check_whitespace() {
    log "whitespace check (code/build files)"
    local paths=('*.c' '*.h' '*.sh' '*.py' '*.yml' '*.yaml' '*.cmake' 'CMakeLists.txt' 'Makefile')
    git diff --check -- "${paths[@]}"
    git diff --cached --check -- "${paths[@]}"
}

run_host_tests() {
    local dir="$1"
    local label="$2"
    shift 2
    log "host tests: $label"
    cmake -S tests/host -B "$dir" "$@"
    cmake --build "$dir"
    ctest --test-dir "$dir" --output-on-failure
}

ensure_idf() {
    if command -v idf.py >/dev/null 2>&1; then
        return 0
    fi
    local idf_path="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.5}"
    if [ ! -f "$idf_path/export.sh" ]; then
        echo "error: ESP-IDF not found at $idf_path" >&2
        echo "set IDF_PATH or install ESP-IDF v5.5.5" >&2
        return 1
    fi
    log "loading ESP-IDF from $idf_path"
    set +u
    unset VIRTUAL_ENV PYTHONHOME PYTHONPATH 2>/dev/null || true
    if [ -d /opt/homebrew/bin ]; then
        export PATH="/opt/homebrew/bin:$PATH"
    fi
    # shellcheck disable=SC1091
    source "$idf_path/export.sh" >/dev/null
    set -u
    command -v idf.py >/dev/null 2>&1
}

build_firmware() {
    local name="$1"
    local sdkconfig="$2"
    local defaults="$3"
    local builddir="$4"
    local logfile="${builddir}.log"

    log "firmware build: $name"
    if [ -d "$builddir" ] && [ ! -f "$builddir/CMakeCache.txt" ]; then
        rm -rf "$builddir"
    fi
    if ! idf.py -B "$builddir" \
        -D SDKCONFIG="$sdkconfig" \
        -D SDKCONFIG_DEFAULTS="$defaults" \
        set-target esp32p4 build >"$logfile" 2>&1; then
        echo "firmware build failed; last 40 lines:" >&2
        tail -40 "$logfile" >&2
        return 1
    fi

    local warnings
    warnings=$(grep -c 'warning:' "$logfile" || true)
    if [ "$warnings" -ne 0 ]; then
        echo "firmware build produced $warnings warning(s):" >&2
        grep 'warning:' "$logfile" | head -20 >&2
        return 1
    fi
    grep -E 'binary size|Project build complete' "$logfile" | tail -3 || true
}

device_ci() {
    ensure_idf

    local port="${PORT:-}"
    if [ -z "$port" ]; then
        local candidates=(/dev/cu.usbmodem*)
        if [ ${#candidates[@]} -eq 1 ] && [ -e "${candidates[0]}" ]; then
            port="${candidates[0]}"
        else
            echo "error: pass --port or PORT; serial candidates: ${candidates[*]:-none}" >&2
            return 1
        fi
    fi

    local defaults="sdkconfig.defaults;sdkconfig.rev1_3.defaults"
    local -a expect=()
    if [ -f "$ROOT/sdkconfig.wifi.local" ]; then
        defaults="$defaults;sdkconfig.wifi.local"
        expect=(--expect-wifi)
    fi

    rm -f "$ROOT/sdkconfig.ci-device"
    build_firmware "device" \
        "$ROOT/sdkconfig.ci-device" \
        "$defaults" \
        "$ROOT/build-ci-device"

    log "flash device: $port"
    if ! idf.py -B "$ROOT/build-ci-device" \
        -D SDKCONFIG="$ROOT/sdkconfig.ci-device" \
        -p "$port" flash > "$ROOT/build-ci-device-flash.log" 2>&1; then
        echo "flash failed; last 30 lines:" >&2
        tail -30 "$ROOT/build-ci-device-flash.log" >&2
        return 1
    fi

    log "device test: ${SOAK_SECONDS}s"
    python3 "$ROOT/tools/device_test.py" \
        --port "$port" \
        --seconds "$SOAK_SECONDS" \
        --log "$ROOT/build-ci-device-serial.log" \
        "${expect[@]}"
}

check_whitespace

if [ "$FIRMWARE_ONLY" -eq 0 ]; then
    run_host_tests "build-host-ci" "normal"
    if [ "$ASAN" -eq 1 ]; then
        run_host_tests "build-host-asan" "asan/ubsan" \
            -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    fi
fi

if [ "$HOST_ONLY" -eq 0 ]; then
    ensure_idf
    build_firmware "rev1.3" \
        "$ROOT/sdkconfig.ci" \
        "sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
        "$ROOT/build-ci"

    if [ "$WIFI" -eq 1 ]; then
        build_firmware "rev1.3+wifi" \
            "$ROOT/sdkconfig.ci-wifi" \
            "sdkconfig.defaults;sdkconfig.rev1_3.defaults;sdkconfig.wifi.local.example" \
            "$ROOT/build-ci-wifi"
    fi
fi

if [ "$DEVICE" -eq 1 ]; then
    device_ci
fi

log "CI passed"
