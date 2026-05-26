#pragma once
#include "config.h"
#include "speaker.h"
#include "actions.h"
#include "modes.h"
#include "Adafruit_seesaw.h"
#include <Preferences.h>

void logEvent(const char* fmt, ...);  // defined in webui.h

// Seesaw module is owned by the sketch (SonosEthRemoteP4.ino) and shared here.
extern Adafruit_seesaw ss;
extern bool ssReady;  // true once ss.begin() succeeded and version checks out

// Per-board rotation direction, persisted in NVS. Loaded at boot, mutable at
// runtime via the web UI toggle so each board can be flipped independently.
inline bool loadEncoderInvert() {
  Preferences p;
  if (!p.begin("encoder", true)) return ENCODER_INVERT_DEFAULT;
  bool v = p.getBool("inv", ENCODER_INVERT_DEFAULT);
  p.end();
  return v;
}
inline void saveEncoderInvert(bool v) {
  Preferences p;
  if (!p.begin("encoder", false)) return;
  p.putBool("inv", v);
  p.end();
}
extern bool encoderInvert;

// Per-board volume step (1..10) — how much volume changes per encoder detent.
inline int loadVolumeStep() {
  Preferences p;
  if (!p.begin("encoder", true)) return VOLUME_STEP_DEFAULT;
  int v = p.getInt("step", VOLUME_STEP_DEFAULT);
  p.end();
  if (v < VOLUME_STEP_MIN) v = VOLUME_STEP_MIN;
  if (v > VOLUME_STEP_MAX) v = VOLUME_STEP_MAX;
  return v;
}
inline void saveVolumeStep(int v) {
  if (v < VOLUME_STEP_MIN) v = VOLUME_STEP_MIN;
  if (v > VOLUME_STEP_MAX) v = VOLUME_STEP_MAX;
  Preferences p;
  if (!p.begin("encoder", false)) return;
  p.putInt("step", v);
  p.end();
}
extern int volumeStep;

// =============================================================================
// Button FSM — supports:
//   Nc            (1c..5c)
//   Nc+h          (1c+h..4c+h) — clicks followed by a click-and-hold
//   hold          (no prior clicks; press held >700ms)
//   lh            (no prior clicks; press held >2000ms)
// =============================================================================
struct ButtonFSM {
  int  level       = BTN_IDLE;
  int  lastRaw     = BTN_IDLE;
  unsigned long lastChange = 0;
  unsigned long pressStart = 0;
  unsigned long lastRelease = 0;
  bool inHoldRegion = false;    // press has crossed T_HOLD (still pressed)
  bool gestureFired = false;    // a gesture has already fired this press cycle
  bool wasNcHold    = false;    // last fired was Nc+h (so release is no-op)
  uint8_t clicks    = 0;
};

static ButtonFSM btn;
static unsigned long lastVolCmd = 0;

// Last time the user physically interacted with the knob — rotation OR button
// edge. Read by /api/status so the web UI can light an activity dot, making
// it possible to identify "which one am I touching?" especially when multiple
// installed boards look identical from the outside.
volatile unsigned long lastActivityMs = 0;

// Last fired gesture + whether its mapped action succeeded. The web UI uses
// these to flash the corresponding gesture row green (success) or red (Sonos
// rejected the SOAP call — e.g. Next on a radio source). Cleared by the UI
// after rendering, but we also expire stale events server-side after 2s.
String        lastFiredGid    = "";
bool          lastFiredOk     = false;
unsigned long lastFiredMs     = 0;

// Per-board ring buffer of recent gestures so the fleet hub can show a "pulse"
// — every click / multi-click / hold from every board in one unified feed.
// Drained on each fleetReport call. Capped — heavy usage in a 5-min window
// past this size drops the oldest entries.
struct FleetEvent {
  unsigned long ms;       // board millis() at event time
  char          gid[8];   // "1c", "2c+h", "hold", "lh", etc.
  bool          ok;       // did the mapped action succeed?
};
static constexpr uint8_t FLEET_EVT_MAX = 24;
static FleetEvent fleetEvents[FLEET_EVT_MAX];
static uint8_t    fleetEvtCount = 0;

inline void fleetLogGesture(const char* gid, bool ok) {
  if (fleetEvtCount < FLEET_EVT_MAX) {
    FleetEvent& e = fleetEvents[fleetEvtCount++];
    e.ms = millis();
    strncpy(e.gid, gid, sizeof(e.gid) - 1);
    e.gid[sizeof(e.gid) - 1] = 0;
    e.ok = ok;
  } else {
    // Drop oldest by shifting one down. Cheap at 24 entries.
    for (uint8_t i = 0; i < FLEET_EVT_MAX - 1; i++) fleetEvents[i] = fleetEvents[i + 1];
    FleetEvent& e = fleetEvents[FLEET_EVT_MAX - 1];
    e.ms = millis();
    strncpy(e.gid, gid, sizeof(e.gid) - 1);
    e.gid[sizeof(e.gid) - 1] = 0;
    e.ok = ok;
  }
}

inline void fleetEventsClear() { fleetEvtCount = 0; }

// Rotation activity — coarse for the pulse: total count + when last detent
// happened. The dashboard converts the timestamp into "Xs ago".
volatile unsigned long lastRotationMs = 0;
uint32_t fleetRotEvents = 0;  // total detents (user-perceptible clicks) since boot

// Rotation burst aggregation — single pulse-feed entry per "knob session"
// (continuous rotation, ending after ~1.2s of no activity). Avoids flooding
// the buffer with one entry per detent when someone spins fast.
static int16_t       rotBurstDelta  = 0;
static unsigned long rotBurstFirst  = 0;

inline void fleetMaybeFlushRotBurst() {
  if (rotBurstDelta == 0) return;
  if (millis() - lastRotationMs < 1200) return;  // still mid-burst
  char gid[8];
  snprintf(gid, sizeof(gid), "rot%+d", (int)rotBurstDelta);
  fleetLogGesture(gid, true);
  rotBurstDelta = 0;
  rotBurstFirst = 0;
}

// =============================================================================
// Rotation — Seesaw firmware already debounces & decodes the quadrature, so
// getEncoderDelta() returns net detent change since the last call. We just
// accumulate into `pending` and apply throttling identically to the EC11 build.
// =============================================================================
// EC11 mechanical encoders emit 4 quadrature transitions per physical detent;
// the Seesaw counts every transition. We accumulate sub-detent counts in
// `fractional` so no rotation is lost, and only emit when a full detent passes.
static constexpr int TRANSITIONS_PER_DETENT = 4;

static void processEncoder() {
  static int16_t fractional = 0;
  static int16_t pending    = 0;
  if (!ssReady) return;

  int32_t det = ss.getEncoderDelta();
  if (encoderInvert) det = -det;
  if (det != 0) {
    lastActivityMs = millis();
    lastRotationMs = millis();
    fractional += det;
    int detents = fractional / TRANSITIONS_PER_DETENT;
    fractional -= detents * TRANSITIONS_PER_DETENT;
    pending += detents;
    if (detents != 0) {
      fleetRotEvents += (detents > 0 ? detents : -detents);
      if (rotBurstFirst == 0) rotBurstFirst = millis();
      rotBurstDelta += detents;
    }
  }
  // Flush idle burst into pulse buffer.
  fleetMaybeFlushRotBurst();

  if (!spk.connected()) return;
  if (pending == 0) return;
  if (millis() - lastVolCmd < T_VOL_THROTTLE) return;

  // In VOLUME mode, skip the SOAP round-trip when already saturated at the rail.
  if (currentMode == MODE_VOLUME) {
    if ((spk.volume >= 100 && pending > 0) || (spk.volume <= 0 && pending < 0)) {
      pending = 0;
      return;
    }
  }

  lastVolCmd = millis();
  // VOLUME mode steps by `volumeStep` per detent. BASS/TREBLE step 1 per detent.
  int delta = (currentMode == MODE_VOLUME) ? (pending * volumeStep) : pending;
  pending = 0;

  int beforeVal = currentModeValue();
  int afterVal  = applyRotation(delta);
  logEvent("rot %s %d -> %d", modeName(currentMode), beforeVal, afterVal);
}

// =============================================================================
// Helper: fire a gesture by id (clicks-then-hold = "Nc+h", clicks alone = "Nc",
// standalone hold = "hold", standalone long hold = "lh").
// =============================================================================
static void fireGesture(uint8_t clicks, bool withHold, bool longHold) {
  char gid[8];
  if (longHold && clicks == 0) {
    strcpy(gid, "lh");
  } else if (withHold && clicks == 0) {
    strcpy(gid, "hold");
  } else if (withHold && clicks > 0) {
    snprintf(gid, sizeof(gid), "%uc+h", clicks);
  } else {
    snprintf(gid, sizeof(gid), "%uc", clicks);
  }

  if (!spk.connected()) {
    logEvent("gesture %s ignored (no speaker)", gid);
    lastFiredGid = String(gid);
    lastFiredOk = false;
    lastFiredMs = millis();
    fleetLogGesture(gid, false);
    return;
  }
  GestureMap m = getMapping(gid);
  String aid = m.actionId.length() > 0 ? m.actionId : defaultActionFor(gid);
  if (aid.length() == 0) {
    logEvent("gesture %s -> (unmapped)", gid);
    lastFiredGid = String(gid);
    lastFiredOk = false;   // unmapped is "didn't do anything" — flash red
    lastFiredMs = millis();
    fleetLogGesture(gid, false);
    return;
  }
  logEvent("gesture %s -> %s", gid, aid.c_str());
  bool ok = runAction(aid, m.param);
  lastFiredGid = String(gid);
  lastFiredOk  = ok;
  lastFiredMs  = millis();
  fleetLogGesture(gid, ok);
  if (!aid.startsWith("enter_")) exitMode();
}

// =============================================================================
// Button polling — distinguishes:
//   press+release sequences (clicks)
//   press held past T_HOLD (short hold)
//   press held past T_LONG_HOLD (long hold, only if no prior clicks)
// Click+hold = clicks then a final press held past T_HOLD.
// =============================================================================
static void processButton() {
  if (!ssReady) return;
  unsigned long now = millis();
  // Seesaw switch is active-LOW with an internal pullup we enable in setup().
  // ss.digitalRead returns true when NOT pressed → BTN_IDLE.
  int raw = ss.digitalRead(SS_SWITCH) ? BTN_IDLE : BTN_ACTIVE;

  if (raw != btn.lastRaw) { btn.lastChange = now; btn.lastRaw = raw; lastActivityMs = now; }

  // Debounced edge detection
  if ((now - btn.lastChange) > T_DEBOUNCE && raw != btn.level) {
    btn.level = raw;
    if (btn.level == BTN_ACTIVE) {
      // Press
      btn.pressStart = now;
      btn.inHoldRegion = false;
      btn.gestureFired = false;
      btn.wasNcHold = false;
    } else {
      // Release
      if (btn.inHoldRegion) {
        // We were holding. If Nc+h already fired, this is just cleanup.
        // If standalone hold reached but didn't fire yet (i.e., released before
        // long-hold trigger), fire "hold" now.
        if (!btn.gestureFired) {
          fireGesture(0, true, false);  // "hold"
          btn.gestureFired = true;
        }
        // Reset for next cycle
        btn.clicks = 0;
        btn.inHoldRegion = false;
      } else {
        // Pure click — accumulate, will flush after multi-click window
        if (btn.clicks < 5) btn.clicks++;
        btn.lastRelease = now;
      }
    }
  }

  bool pressed = (btn.level == BTN_ACTIVE);
  unsigned long pressDur = pressed ? (now - btn.pressStart) : 0;

  // Crossing into hold region
  if (pressed && !btn.inHoldRegion && pressDur >= T_HOLD) {
    btn.inHoldRegion = true;
    if (btn.clicks > 0) {
      // Click(s) followed by hold = "Nc+h" — fire immediately at the hold edge
      fireGesture(btn.clicks, true, false);
      btn.gestureFired = true;
      btn.wasNcHold = true;
      btn.clicks = 0;
    }
    // For standalone hold (clicks==0): wait for release OR long-hold trigger.
  }

  // Long-hold trigger (only for standalone hold)
  if (pressed && btn.inHoldRegion && !btn.gestureFired && !btn.wasNcHold
      && pressDur >= T_LONG_HOLD) {
    fireGesture(0, false, true);  // "lh"
    btn.gestureFired = true;
  }

  // Multi-click flush (no hold pending, accumulated clicks, idle window passed)
  if (!pressed && !btn.inHoldRegion && btn.clicks > 0
      && (now - btn.lastRelease) > T_MULTI_CLICK) {
    fireGesture(btn.clicks, false, false);
    btn.clicks = 0;
  }
}
