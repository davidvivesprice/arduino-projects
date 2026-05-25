#pragma once
#include <Arduino.h>
#include "config.h"
#include "speaker.h"

// =============================================================================
// Encoder modes — VOLUME (default) / BASS / TREBLE.
// In a non-default mode, rotation adjusts that mode's value instead of volume.
// Auto-exits to VOLUME after T_MODE_TIMEOUT (5s) of no rotation.
// LED blinks differently per mode for knob-only feedback.
// =============================================================================
enum EncoderMode : uint8_t {
  MODE_VOLUME = 0,
  MODE_BASS,
  MODE_TREBLE,
};

static EncoderMode currentMode = MODE_VOLUME;
static unsigned long modeLastActivity = 0;
static int modeBassCache = 0;     // last known bass (refreshed on entry)
static int modeTrebleCache = 0;   // last known treble

inline const char* modeName(EncoderMode m) {
  switch (m) {
    case MODE_VOLUME: return "volume";
    case MODE_BASS:   return "bass";
    case MODE_TREBLE: return "treble";
  }
  return "?";
}

inline void enterMode(EncoderMode m) {
  if (currentMode == m) return;
  currentMode = m;
  modeLastActivity = millis();
  // Cache current bass/treble so we know where to start
  if (spk.connected()) {
    ctrl.ip = spk.ip;
    if (m == MODE_BASS)   modeBassCache   = ctrl.getBass();
    if (m == MODE_TREBLE) modeTrebleCache = ctrl.getTreble();
  }
}

inline void exitMode() {
  if (currentMode != MODE_VOLUME) {
    currentMode = MODE_VOLUME;
  }
}

// Called by encoder rotation handler — returns the new value after applying delta.
// Updates internal cache and sends SOAP for bass/treble.
inline int applyRotation(int delta) {
  modeLastActivity = millis();
  if (!spk.connected()) return 0;
  ctrl.ip = spk.ip;

  switch (currentMode) {
    case MODE_VOLUME:
      return adjustVolume(delta);

    case MODE_BASS: {
      modeBassCache = constrain(modeBassCache + delta, -10, 10);
      ctrl.setBass(modeBassCache);
      return modeBassCache;
    }

    case MODE_TREBLE: {
      modeTrebleCache = constrain(modeTrebleCache + delta, -10, 10);
      ctrl.setTreble(modeTrebleCache);
      return modeTrebleCache;
    }
  }
  return 0;
}

// Call from main loop. Auto-exits mode after timeout, drives LED blink.
inline void modeTick() {
  // Auto-exit
  if (currentMode != MODE_VOLUME &&
      (millis() - modeLastActivity) > T_MODE_TIMEOUT) {
    currentMode = MODE_VOLUME;
  }

  // LED feedback (single GPIO, on/off pattern)
  static unsigned long lastLed = 0;
  static bool ledState = false;
  unsigned long now = millis();
  unsigned long period = 0;

  switch (currentMode) {
    case MODE_VOLUME: digitalWrite(PIN_LED, HIGH); return;     // solid on
    case MODE_BASS:   period = 500; break;                     // 1 Hz
    case MODE_TREBLE: period = 167; break;                     // ~3 Hz
  }

  if (now - lastLed >= period) {
    lastLed = now;
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  }
}

inline int currentModeValue() {
  switch (currentMode) {
    case MODE_VOLUME: return spk.volume;
    case MODE_BASS:   return modeBassCache;
    case MODE_TREBLE: return modeTrebleCache;
  }
  return 0;
}
