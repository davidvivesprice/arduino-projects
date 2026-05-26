# SonosEthRemoteP4 — Changelog

All notable changes to this firmware, version by version. Format roughly follows
[Keep a Changelog](https://keepachangelog.com/). Each release ties to a GitHub
Release tagged `sonos-eth-p4-v<version>` with the binary attached.

---

## [1.0.4] — pending

### Added
- **Encoder hot-swap.** Firmware probes the I2C bus every 2 s. If a Seesaw
  appears (encoder freshly plugged in), it's auto-initialized and rotation
  works immediately — no reboot needed. If it disappears (3 consecutive ACK
  failures), the board flags `i2c: false` and the hub dashboard goes red for
  that room.
- **Up-front fleet health verdict** at the top of the dashboard. Big green
  banner "All N boards healthy" or specific issues called out ("Bedroom 3:
  no encoder · Master Bath: no speaker").
- **Rich pulse labels.** Gestures show as "tap", "double-tap", "long hold",
  "tap + hold", etc. Rotations classify by magnitude — "vol up · light (+2)",
  "vol up · medium (+5)", "vol up · fast spin (+12)". Failed actions append
  "didn't fire" in red.

---

## [1.0.3] — 2026-05-26

### Added
- Rotation bursts surface in the fleet hub pulse feed (one entry per "knob
  session", aggregated by 1.2s idle gap; format `rot+N` / `rot-N`).
- Activity-triggered fleet reporting — boards push to the hub within ~25 s of
  any gesture or rotation burst instead of waiting for the 5-min heartbeat,
  so the dashboard pulse feels live.

### Changed
- Firmware version line in per-board web UI bumped from 9 px ink-3 to 11 px
  ink + uppercase tracking — actually visible at a glance now.

---

## [1.0.2] — 2026-05-26

### Added
- **Fleet Hub** observability — Cloudflare Worker at
  `sonos-fleet-hub.davidvivesprice.workers.dev` ingests POSTs from every board
  every 5 min, stores latest state + rolling 24 h gesture history in KV, and
  serves a live dashboard.
- Firmware-side `fleet.h` reporter: posts id / room / fw / uptime / i2c status
  / speaker name & online / rotation counter / last-rot timestamp / cumulative
  error counters / gesture events ring buffer.
- Dashboard liveness grid + chronological **pulse feed** of clicks and holds
  across the entire fleet, with failed actions rendered red.
- Per-board card shows total rotation count + "last knob touched" age.
- Shared-secret auth on hub ingest (`X-Fleet-Auth`). Secret lives in
  `config_secrets.h` (gitignored) — missing file = silent no-op reporter.

---

## [1.0.1] — 2026-05-26

### Added
- **Per-gesture UI flash** — when a gesture fires the matching mapping row
  glows green on success or red on Sonos rejection (1.6 s fade). Surfaces the
  actual outcome of every click / multi-click / hold in real time.
- **Configurable hostname prefix** — `<prefix>-<slug>.local`, NVS-stored.
  Default `tpsvc`. Phil's home install uses `phil` giving `phil-liv.local`,
  cleanly separated from the Martha's Vineyard `tpsvc-*` fleet.
- `SETUP:<prefix>:<slug>` USB-serial command (atomic prefix + slug, one
  restart). `flash.sh --prefix` passes it through.
- `liv` (Living Room) and `kit` (Kitchen) added to canonical room labels.
- `/api/setprefix` and `/api/setup` HTTP endpoints for runtime reassignment
  without re-flashing over USB.

### Changed
- **OTA poll cadence 6 h → 5 min ±30 s** so a release pushed from anywhere
  propagates to every board within ~5–6 min. ~108 manifest GETs/hr across a
  9-board fleet, well under any rate limit.
- Transport methods (`play` / `pause` / `next` / `previous` / `setMute`) now
  return the SOAP response string. `togglePlay` / `nextTrack` / `prevTrack`
  / `toggleMute` report real Sonos behavior instead of always `true`.
- `getVolume()` returns -1 on real SOAP failure; `refreshState()` requires
  3 consecutive failures before flagging the speaker offline. Eliminates the
  rapid-rotation false-positive that previously triggered 5-min auto-rediscovery.

### Fixed
- SOAP errors now visible in `/api/log` with parsed errorCode (e.g.
  `SOAP Next -> 500 err=800`). Sonos source refusals (radio rejecting
  Next/Previous) are no longer silent.

---

## [1.0.0] — 2026-05-25

### Added
- Initial release of the M5 Unit PoE-P4 build of the Sonos Ethernet Remote.
- Adafruit Seesaw I2C rotary encoder (PID 4991) on the Grove port (G53/G54).
- Native IP101GRI RMII Ethernet on the M5 Unit PoE-P4.
- Direct SOAP control of Sonos — no external library.
- Web UI with dial gauge, transport, mode pills, gesture mapping, sound EQ,
  speaker selector, live log.
- mDNS hostname per board: `tpsvc-<slug>.local` for canonical Martha's
  Vineyard rooms (mstbed, mstbth, grkit, grliv, fam, bed2, bed3).
- Per-board NVS-persisted room assignment via `flash.sh <slug>` (USB serial
  `ROOM:<slug>` command) or `/api/setroom`.
- Per-board NVS-persisted rotation direction (`encoderInvert`) + volume step
  (1–10), exposed as toggle + slider in the web UI.
- Activity dot in the masthead — green pulse on each rotation/click so you
  can identify "which board am I touching?" remotely from the web UI.
- **OTA pull-update pipeline** — boards check `manifest.json` on
  `raw.githubusercontent.com` periodically, download new firmware from the
  matching GitHub Release asset, verify SHA-256, install via `Update.h`,
  reboot. ESP32 dual-partition rollback covers failed boots.
- `release.sh` build + publish: bumps `FW_VERSION`, compiles, computes
  SHA-256, creates GitHub Release with the binary, rewrites `manifest.json`,
  commits + tags + pushes.
- A4 printable bag inserts (logo + room + QR + setup instructions) at
  `inserts/all_cards.pdf`, generated by `build_inserts.py`.
