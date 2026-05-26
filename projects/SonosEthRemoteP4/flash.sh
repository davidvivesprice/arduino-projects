#!/usr/bin/env bash
# Flash a Unit PoE-P4 over USB and assign it a room in one shot.
#
#   ./flash.sh <slug>            # compile-if-needed + upload + ROOM:<slug>
#   ./flash.sh --rebuild <slug>  # force a clean compile first
#   ./flash.sh --list            # show the canonical room slugs
#   ./flash.sh --room-only <slug># skip upload, just send ROOM (board already flashed)
#
# After flashing the script sends "ROOM:<slug>\n" over USB serial repeatedly
# for up to 12 seconds. The firmware accepts the command at any time post-boot
# (it no longer halts on missing ETH), so even if a few retries are eaten by
# CDC re-enumeration, one will land. On receipt the firmware writes NVS and
# restarts. Once you plug into PoE, the board comes up as tpsvc-<slug>.local.

set -euo pipefail
cd "$(dirname "$0")"

FQBN=esp32:esp32:esp32p4
BIN=./build/SonosEthRemoteP4.ino.bin

# Canonical rooms — must mirror room.h. flash.sh accepts any valid slug, but
# warns on unknown ones so typos don't silently make tpsvc-mstbedd ghosts.
KNOWN_SLUGS=(mstbed mstbth grkit grliv fam bed2 bed3)

show_list() {
  echo "Canonical rooms:"
  for s in "${KNOWN_SLUGS[@]}"; do echo "  $s"; done
}

usage() {
  echo "usage: ./flash.sh [--rebuild|--room-only] <slug>"
  echo "       ./flash.sh --list"
  exit 1
}

[[ $# -eq 0 || "${1:-}" == "--help" ]] && usage

REBUILD=0; ROOM_ONLY=0; PREFIX=""
while [[ "${1:-}" == --* ]]; do
  case "$1" in
    --rebuild)   REBUILD=1; shift ;;
    --room-only) ROOM_ONLY=1; shift ;;
    --prefix)    PREFIX="${2:-}"; shift 2 ;;
    --list)      show_list; exit 0 ;;
    --help)      usage ;;
    *)           echo "unknown flag $1"; usage ;;
  esac
done

SLUG="${1:-}"
[[ -z "$SLUG" ]] && { echo "missing slug"; exit 1; }
[[ "$SLUG" =~ ^[a-z0-9-]{1,16}$ ]] || { echo "invalid slug '$SLUG' (a-z 0-9 -, ≤16 chars)"; exit 1; }
[[ -n "$PREFIX" && ! "$PREFIX" =~ ^[a-z0-9-]{1,16}$ ]] && { echo "invalid prefix '$PREFIX'"; exit 1; }

# Warn on uncanonical slugs.
known=0
for s in "${KNOWN_SLUGS[@]}"; do [[ "$s" == "$SLUG" ]] && known=1; done
[[ $known -eq 0 ]] && echo "(warning: '$SLUG' isn't in the canonical room list — proceeding)"

PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1 || true)
[[ -z "$PORT" ]] && { echo "no /dev/cu.usbmodem* found — is the board plugged in via USB?"; exit 1; }
if [[ -n "$PREFIX" ]]; then
  echo "port: $PORT  → assigning prefix='$PREFIX' slug='$SLUG' → ${PREFIX}-${SLUG}.local"
else
  echo "port: $PORT  → assigning slug '$SLUG' → tpsvc-${SLUG}.local (default prefix)"
fi

if [[ $ROOM_ONLY -eq 0 ]]; then
  if [[ $REBUILD -eq 1 || ! -f "$BIN" ]]; then
    echo "compiling..."
    arduino-cli compile --fqbn $FQBN --output-dir ./build .
  fi
  echo "uploading via USB..."
  arduino-cli upload --fqbn $FQBN --port "$PORT" --input-dir ./build .
  echo "waiting for CDC re-enumeration..."
  sleep 4
  # Re-scan — the device number may change after the reset.
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1 || true)
  [[ -z "$PORT" ]] && { echo "USB port disappeared after upload"; exit 2; }
fi

# Reliable serial send: hold the FD open in Python and retry every 750ms for
# up to 12 seconds. The firmware ACK-restarts on first successful read; once
# the board reboots the CDC drops and subsequent writes silently fail, which
# is the intended exit signal.
if [[ -n "$PREFIX" ]]; then
  CMD="SETUP:${PREFIX}:${SLUG}"
else
  CMD="ROOM:${SLUG}"
fi
echo "sending ${CMD} (retrying for up to 12s)..."
python3 - "$PORT" "$CMD" <<'PY'
import os, sys, time, errno
port, cmd = sys.argv[1], sys.argv[2]
payload = (f"{cmd}\r\n").encode()
deadline = time.time() + 12.0
sent = 0
fd = None
while time.time() < deadline:
    try:
        if fd is None:
            fd = os.open(port, os.O_WRONLY | os.O_NOCTTY | os.O_NONBLOCK)
        os.write(fd, payload)
        sent += 1
    except OSError as e:
        # Port went away mid-stream — usually means the firmware accepted the
        # command and restarted. That's a success signal.
        if e.errno in (errno.ENOENT, errno.ENXIO, errno.EIO, errno.ENODEV, errno.EBADF):
            if sent > 0:
                print(f"  port closed after {sent} send(s) — board likely restarted ✓")
                sys.exit(0)
            try: os.close(fd)
            except: pass
            fd = None
        else:
            raise
    time.sleep(0.75)
if sent == 0:
    print("  no successful writes — board may not be enumerated as CDC", file=sys.stderr)
    sys.exit(3)
print(f"  sent {sent} times — port stayed open the whole time (firmware may have already restarted earlier ✓)")
PY

if [[ -n "$PREFIX" ]]; then
  echo "done. Unplug USB, plug into PoE → expected mDNS: ${PREFIX}-${SLUG}.local"
else
  echo "done. Unplug USB, plug into PoE → expected mDNS: tpsvc-${SLUG}.local"
fi
