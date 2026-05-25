#pragma once
#include <Preferences.h>
#include "speaker.h"
#include "modes.h"

// =============================================================================
// Action registry — every Sonos action exposed for gesture mapping & web UI.
// =============================================================================
struct ActionDef {
  const char* id;        // stable id used in NVS + URL params
  const char* label;     // human label
  const char* category;  // playback | volume | eq | speaker | sleep | mode
  const char* paramHint; // "" | "0..100" | "-10..10" | "NORMAL|SHUFFLE|..."
};

static const ActionDef ACTIONS[] = {
  // Playback
  {"toggle_play",   "Play / Pause",       "playback", ""},
  {"play",          "Play",               "playback", ""},
  {"pause",         "Pause",              "playback", ""},
  {"stop",          "Stop",               "playback", ""},
  {"next",          "Next track",         "playback", ""},
  {"prev",          "Previous track",     "playback", ""},

  // Volume
  {"vol_up",        "Volume +2",          "volume",   ""},
  {"vol_down",      "Volume -2",          "volume",   ""},
  {"vol_step",      "Volume step",        "volume",   "-20..20"},
  {"set_vol",       "Set volume",         "volume",   "0..100"},
  {"mute_toggle",   "Toggle mute",        "volume",   ""},
  {"mute_on",       "Mute",               "volume",   ""},
  {"mute_off",      "Unmute",             "volume",   ""},

  // EQ
  {"bass_up",       "Bass +1",            "eq",       ""},
  {"bass_down",     "Bass -1",            "eq",       ""},
  {"set_bass",      "Set bass",           "eq",       "-10..10"},
  {"treble_up",     "Treble +1",          "eq",       ""},
  {"treble_down",   "Treble -1",          "eq",       ""},
  {"set_treble",    "Set treble",         "eq",       "-10..10"},
  {"loudness_on",   "Loudness on",        "eq",       ""},
  {"loudness_off",  "Loudness off",       "eq",       ""},

  // Speaker
  {"cycle_speaker", "Cycle to next speaker", "speaker", ""},
  {"refresh_state", "Refresh state",      "speaker",  ""},

  // Encoder modes (knob rotation behavior)
  {"enter_bass",    "Knob → Bass",        "knob",     ""},
  {"enter_treble",  "Knob → Treble",      "knob",     ""},
  {"enter_volume",  "Knob → Volume",      "knob",     ""},

  // Play modes
  {"mode_normal",   "Mode: Normal",       "playmode", ""},
  {"mode_shuffle",  "Mode: Shuffle",      "playmode", ""},
  {"mode_repeat",   "Mode: Repeat all",   "playmode", ""},
  {"mode_repeat1",  "Mode: Repeat one",   "playmode", ""},
  {"crossfade_on",  "Crossfade on",       "playmode", ""},
  {"crossfade_off", "Crossfade off",      "playmode", ""},

  // Sleep
  {"sleep_15",      "Sleep 15 min",       "sleep",    ""},
  {"sleep_30",      "Sleep 30 min",       "sleep",    ""},
  {"sleep_60",      "Sleep 1 hr",         "sleep",    ""},
  {"sleep_off",     "Cancel sleep",       "sleep",    ""},
};
constexpr size_t ACTIONS_COUNT = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

// =============================================================================
// Central action dispatcher.
// Returns true on success. Param is optional (used by params hint actions).
// =============================================================================
inline bool runAction(const String& id, const String& param) {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;

  // Playback
  if (id == "toggle_play")    return togglePlay();
  if (id == "play")           { ctrl.play();      spk.playing = true;  return true; }
  if (id == "pause")          { ctrl.pause();     spk.playing = false; return true; }
  if (id == "stop")           { ctrl.stop();      spk.playing = false; return true; }
  if (id == "next")           return nextTrack();
  if (id == "prev")           return prevTrack();

  // Volume
  if (id == "vol_up")         { adjustVolume(2);  return true; }
  if (id == "vol_down")       { adjustVolume(-2); return true; }
  if (id == "vol_step")       { adjustVolume(param.toInt()); return true; }
  if (id == "set_vol")        return setVolume(param.toInt());
  if (id == "mute_toggle")    return toggleMute();
  if (id == "mute_on")        { ctrl.setMute(true);  spk.muted = true;  return true; }
  if (id == "mute_off")       { ctrl.setMute(false); spk.muted = false; return true; }

  // EQ
  if (id == "bass_up")        { ctrl.setBass(constrain(ctrl.getBass() + 1, -10, 10)); return true; }
  if (id == "bass_down")      { ctrl.setBass(constrain(ctrl.getBass() - 1, -10, 10)); return true; }
  if (id == "set_bass")       { ctrl.setBass(param.toInt()); return true; }
  if (id == "treble_up")      { ctrl.setTreble(constrain(ctrl.getTreble() + 1, -10, 10)); return true; }
  if (id == "treble_down")    { ctrl.setTreble(constrain(ctrl.getTreble() - 1, -10, 10)); return true; }
  if (id == "set_treble")     { ctrl.setTreble(param.toInt()); return true; }
  if (id == "loudness_on")    { ctrl.setLoudness(true);  return true; }
  if (id == "loudness_off")   { ctrl.setLoudness(false); return true; }

  // Speaker
  if (id == "cycle_speaker")  { cycleNext(); return true; }
  if (id == "refresh_state")  return refreshState();

  // Encoder modes
  if (id == "enter_bass")     { enterMode(MODE_BASS);   return true; }
  if (id == "enter_treble")   { enterMode(MODE_TREBLE); return true; }
  if (id == "enter_volume")   { exitMode(); return true; }

  // Modes
  if (id == "mode_normal")    { ctrl.setPlayMode("NORMAL"); return true; }
  if (id == "mode_shuffle")   { ctrl.setPlayMode("SHUFFLE"); return true; }
  if (id == "mode_repeat")    { ctrl.setPlayMode("REPEAT_ALL"); return true; }
  if (id == "mode_repeat1")   { ctrl.setPlayMode("REPEAT_ONE"); return true; }
  if (id == "crossfade_on")   { ctrl.setCrossfade(true);  return true; }
  if (id == "crossfade_off")  { ctrl.setCrossfade(false); return true; }

  // Sleep
  if (id == "sleep_15")       { ctrl.setSleepTimer("00:15:00"); return true; }
  if (id == "sleep_30")       { ctrl.setSleepTimer("00:30:00"); return true; }
  if (id == "sleep_60")       { ctrl.setSleepTimer("01:00:00"); return true; }
  if (id == "sleep_off")      { ctrl.setSleepTimer(""); return true; }

  return false;
}

// =============================================================================
// Gesture → action mapping (NVS persistence)
// =============================================================================
// Gesture ids: "1c" "2c" "3c" "4c" "hold"
// Stored as "actionId|param" in NVS namespace "gesture"
struct GestureMap {
  String actionId;
  String param;
};

inline GestureMap getMapping(const char* gestureId) {
  Preferences p;
  p.begin("gesture", true);
  String raw = p.getString(gestureId, "");
  p.end();
  GestureMap m;
  int sep = raw.indexOf('|');
  if (sep >= 0) { m.actionId = raw.substring(0, sep); m.param = raw.substring(sep + 1); }
  else m.actionId = raw;
  return m;
}

inline void setMapping(const char* gestureId, const String& actionId, const String& param) {
  Preferences p;
  p.begin("gesture", false);
  String v = actionId;
  if (param.length() > 0) { v += '|'; v += param; }
  p.putString(gestureId, v);
  p.end();
}

inline void clearMapping(const char* gestureId) {
  Preferences p;
  p.begin("gesture", false);
  p.remove(gestureId);
  p.end();
}

// Default mappings if user hasn't set any
inline String defaultActionFor(const char* gestureId) {
  if (!strcmp(gestureId, "1c"))    return "toggle_play";
  if (!strcmp(gestureId, "2c"))    return "next";
  if (!strcmp(gestureId, "3c"))    return "prev";
  if (!strcmp(gestureId, "4c"))    return "";  // intentionally unmapped — assign per-board in UI
  if (!strcmp(gestureId, "5c"))    return "";
  if (!strcmp(gestureId, "hold"))  return "mute_toggle";
  if (!strcmp(gestureId, "lh"))    return "enter_volume";   // long-hold = exit any mode
  if (!strcmp(gestureId, "1c+h"))  return "enter_bass";
  if (!strcmp(gestureId, "2c+h"))  return "enter_treble";
  if (!strcmp(gestureId, "3c+h"))  return "";
  if (!strcmp(gestureId, "4c+h"))  return "";
  return "";
}

// All gestures, in display order
static const char* GESTURE_IDS[] = {
  "1c", "2c", "3c", "4c", "5c",
  "1c+h", "2c+h", "3c+h", "4c+h",
  "hold", "lh"
};
constexpr size_t GESTURE_COUNT = sizeof(GESTURE_IDS) / sizeof(GESTURE_IDS[0]);

inline const char* gestureLabel(const char* id) {
  if (!strcmp(id, "1c"))   return "1 click";
  if (!strcmp(id, "2c"))   return "2 clicks";
  if (!strcmp(id, "3c"))   return "3 clicks";
  if (!strcmp(id, "4c"))   return "4 clicks";
  if (!strcmp(id, "5c"))   return "5 clicks";
  if (!strcmp(id, "1c+h")) return "click + hold";
  if (!strcmp(id, "2c+h")) return "2c + hold";
  if (!strcmp(id, "3c+h")) return "3c + hold";
  if (!strcmp(id, "4c+h")) return "4c + hold";
  if (!strcmp(id, "hold")) return "hold (700ms)";
  if (!strcmp(id, "lh"))   return "long hold (2s)";
  return id;
}

inline bool runGesture(const char* gestureId) {
  GestureMap m = getMapping(gestureId);
  if (m.actionId.length() == 0) m.actionId = defaultActionFor(gestureId);
  if (m.actionId.length() == 0) return false;
  bool ok = runAction(m.actionId, m.param);
  // Any gesture that isn't itself a mode-entry should exit any active mode.
  // This matches the rule: "non-rotation gesture exits to VOLUME".
  if (!m.actionId.startsWith("enter_")) exitMode();
  return ok;
}
