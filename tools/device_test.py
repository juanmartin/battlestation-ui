#!/usr/bin/env python3
"""Capture a board boot/soak and check the local device CI gates."""

import argparse
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    print("error: pyserial is required (run inside the ESP-IDF environment)", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--log", default="build-ci-device/serial.log")
    parser.add_argument("--expect-wifi", action="store_true")
    args = parser.parse_args()

    os.makedirs(os.path.dirname(args.log) or ".", exist_ok=True)
    ser = serial.Serial(args.port, 115200, timeout=1)
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.2)
    ser.setRTS(False)
    time.sleep(0.2)

    log = open(args.log, "w", encoding="utf-8", errors="replace")
    started = time.time()
    boot = False
    audio = False
    analysis = False
    wifi = False
    errors = 0
    panic = False
    stall = False
    heartbeats = []
    max_stall = 0

    try:
        while time.time() - started < args.seconds:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").rstrip()
            log.write(line + "\n")
            log.flush()

            if "app_manager: mount 'launcher'" in line:
                boot = True
            if "audio_service: ES7210 capture" in line:
                audio = True
            if "audio_analysis: L=" in line:
                analysis = True
            if "wifi_probe: connected" in line:
                wifi = True
            if line.startswith("E ("):
                errors += 1
            if any(k in line for k in ("Guru Meditation", "PANIC", "abort()")):
                panic = True
            if "UI STALL" in line:
                stall = True

            m = re.search(r"diag_probe: heartbeat=(\d+).*stall=(\d+)", line)
            if m:
                heartbeats.append(int(m.group(1)))
                max_stall = max(max_stall, int(m.group(2)))
    finally:
        log.close()
        ser.close()

    heartbeat_ok = len(heartbeats) >= 2 and heartbeats[-1] > heartbeats[0]
    passed = (
        boot
        and audio
        and analysis
        and not panic
        and not stall
        and errors == 0
        and heartbeat_ok
        and max_stall == 0
        and (wifi or not args.expect_wifi)
    )

    print(f"device:          {args.port}")
    print(f"duration:        {args.seconds}s")
    print(f"boot/launcher:   {'yes' if boot else 'NO'}")
    print(f"audio capture:   {'yes' if audio else 'NO'}")
    print(f"audio analysis:  {'yes' if analysis else 'NO'}")
    if heartbeats:
        print(f"heartbeat:       {heartbeats[0]} -> {heartbeats[-1]} ({len(heartbeats)} samples)")
    else:
        print("heartbeat:       NO SAMPLES")
    print(f"max stall:       {max_stall}")
    print(f"wifi:            {'connected' if wifi else ('NOT CONNECTED (expected)' if args.expect_wifi else 'not required')}")
    print(f"errors/panic:    {errors}/{('yes' if panic else 'no')}")
    print(f"result:          {'PASS' if passed else 'FAIL'}")
    print(f"log:             {args.log}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
