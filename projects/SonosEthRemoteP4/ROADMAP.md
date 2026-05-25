# Sonos ETH Remote — Roadmap

We iterate one feature at a time. This is the master list.

---

## Gesture vocabulary (agreed)

The encoder + button supports these gestures. "Click after hold" is intentionally
NOT a pattern (feels wrong). All gestures are clicks-then-optional-hold.

| ID    | Gesture                                  |
|-------|------------------------------------------|
| `1c`  | 1 click                                  |
| `2c`  | 2 clicks                                 |
| `3c`  | 3 clicks                                 |
| `4c`  | 4 clicks                                 |
| `5c`  | 5 clicks                                 |
| `1c+h`| Click, then click-and-hold (>700ms)      |
| `2c+h`| Click, click, then click-and-hold        |
| `3c+h`| Click × 3, then click-and-hold           |
| `4c+h`| Click × 4, then click-and-hold           |
| `hold`| Standalone hold (>700ms, no prior click) |
| `lh`  | Standalone long hold (>2000ms)           |

Rotation is a separate input. What rotation does depends on the current **mode**.

---

## Modes (encoder rotation behavior)

| Mode    | Rotation does     | LED               |
|---------|-------------------|-------------------|
| VOLUME  | Adjust volume     | Solid             |
| BASS    | Adjust bass −10..+10  | Slow blink (1Hz) |
| TREBLE  | Adjust treble −10..+10 | Fast blink (3Hz) |

Modes auto-exit to VOLUME after 5s of no rotation. Any non-rotation gesture also
exits to VOLUME after firing.

---

## Iterations

### ✅ Iteration 0 — Foundation
- Direct SOAP control, mDNS discovery, web UI, OTA, gauge, transport, now-playing

### ▶ Iteration 1 — Modes + richer gestures + sliders ← **CURRENT**
- Mode state machine (VOLUME / BASS / TREBLE), 5s auto-exit
- Click+hold gesture detection (1c+h, 2c+h, 3c+h, 4c+h)
- Long-hold detection (lh, >2s)
- LED feedback for modes
- Bass/Treble/Loudness sliders in web UI
- Current Mode indicator near gauge
- "Test gesture" buttons in mapping rows (so we can feel the assignments before encoder is wired)
- Cleaner actions library layout (no overlap)

### ⏭ Iteration 2 — Macros / Presets
- Snapshot current speaker state (vol, mute, mode, playing) → save with a name
- Restore preset by name
- Bind a preset to a gesture (preset = an action you can map)
- Built-ins: "Quiet" (vol 10), "Loud" (vol 60), "Movie" (bass +2, crossfade off)

### Iteration 3 — Queue + song progress
- Live queue list (Browse Q:0), tap to jump to track
- Progress bar with elapsed/duration
- Drag to seek (REL_TIME)

### Iteration 4 — Favorites + Playlists
- One-tap Favorites grid (Browse FV:2)
- Playlist picker (Browse SQ:)
- "Play radio URL" direct entry

### Iteration 5 — Multi-room / grouping
- Coordinator detection (so play/pause works on followers)
- Group view (which speakers are grouped)
- Drag a speaker into a group / un-group
- Group volume slider (vs individual)

### Iteration 6 — Real-time UPnP eventing
- SUBSCRIBE to RenderingControl + AVTransport events
- Replace 1s polling with push-based updates
- Instant volume/playback reflection (changes from Sonos app, voice, other controllers)

### Iteration 7 — Sound shaping (full)
- Night mode / Dialog boost (soundbars only)
- Crossfade toggle in main UI
- TruePlay status indicator
- Surround levels for home theater

### Iteration 8 — Alarms
- List/create/edit alarms (AlarmClock service)
- Snooze
- "Wake to favorite at 7 AM" presets

### Iteration 9 — Power user
- SOAP console (type service/action/params, see response)
- Network info / firmware / battery (`/status/info` JSON)
- Multi-controller coordination

### Iteration 10 — Polish
- Light/dark theme + accent color
- Per-device persistent layout state
- Mobile-optimized large-touch mode
