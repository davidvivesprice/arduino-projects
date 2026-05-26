#pragma once
#include <HTTPClient.h>
#include <Preferences.h>
#include "config.h"

void logEvent(const char* fmt, ...);  // defined in webui.h

// =============================================================================
// SonosController — direct SOAP, no library dependency
// =============================================================================
class SonosController {
public:
  String ip;

  SonosController() {}
  SonosController(const String& speakerIP) : ip(speakerIP) {}

  String soap(const char* endpoint, const char* serviceType,
              const char* action, const char* params) {
    HTTPClient http;
    http.setTimeout(3000);
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400%s", ip.c_str(), endpoint);
    http.begin(url);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");

    char soapAction[192];
    snprintf(soapAction, sizeof(soapAction), "\"%s#%s\"", serviceType, action);
    http.addHeader("soapaction", soapAction);

    char body[600];
    snprintf(body, sizeof(body),
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
      " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>",
      action, serviceType, params, action);

    String resp;
    int code = http.POST(body);
    if (code == 200) {
      resp = http.getString();
    } else {
      String errBody = http.getString();
      // Surface SOAP failures in the web log so we can see the source's
      // refusal (e.g. Sonos Radio rejecting Next with errorCode 701).
      String fault = tag(errBody, "errorCode");
      if (fault.length() > 0) {
        logEvent("SOAP %s -> %d err=%s", action, code, fault.c_str());
      } else {
        logEvent("SOAP %s -> HTTP %d", action, code);
      }
      dbg("SOAP %s -> %d body=%s", action, code, errBody.c_str());
    }
    http.end();
    return resp;
  }

  // Service shorthands
  String rc(const char* action, const char* params) {
    return soap("/MediaRenderer/RenderingControl/Control",
                "urn:schemas-upnp-org:service:RenderingControl:1", action, params);
  }
  String av(const char* action, const char* params) {
    return soap("/MediaRenderer/AVTransport/Control",
                "urn:schemas-upnp-org:service:AVTransport:1", action, params);
  }

  // XML extraction
  static String tag(const String& xml, const char* t) {
    String open = String("<") + t + ">";
    int s = xml.indexOf(open);
    if (s < 0) return "";
    s += open.length();
    int e = xml.indexOf(String("</") + t + ">", s);
    return (e > s) ? xml.substring(s, e) : "";
  }

  // --- RenderingControl ---
  // Returns the current Sonos volume (0..100) or -1 if the SOAP call failed
  // outright (timeout, non-200, missing tag). The -1 sentinel lets callers
  // distinguish "user legitimately set vol=0" from "speaker unreachable".
  int getVolume() {
    String resp = rc("GetVolume", "<InstanceID>0</InstanceID><Channel>Master</Channel>");
    if (resp.length() == 0) return -1;        // HTTP failure
    String v = tag(resp, "CurrentVolume");
    if (v.length() == 0) return -1;           // unexpected response shape
    return v.toInt();
  }

  void setVolume(int vol) {
    char p[96];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "<DesiredVolume>%d</DesiredVolume>", constrain(vol, 0, 100));
    rc("SetVolume", p);
  }

  int setRelativeVolume(int adj) {
    char p[96];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "<Adjustment>%d</Adjustment>", adj);
    return tag(rc("SetRelativeVolume", p), "NewVolume").toInt();
  }

  bool getMute() {
    return tag(rc("GetMute", "<InstanceID>0</InstanceID><Channel>Master</Channel>"),
               "CurrentMute") == "1";
  }

  String setMute(bool m) {
    char p[96];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "<DesiredMute>%d</DesiredMute>", m ? 1 : 0);
    return rc("SetMute", p);
  }

  int getBass() {
    return tag(rc("GetBass", "<InstanceID>0</InstanceID>"), "CurrentBass").toInt();
  }
  void setBass(int v) {
    char p[64];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><DesiredBass>%d</DesiredBass>", constrain(v, -10, 10));
    rc("SetBass", p);
  }
  int getTreble() {
    return tag(rc("GetTreble", "<InstanceID>0</InstanceID>"), "CurrentTreble").toInt();
  }
  void setTreble(int v) {
    char p[64];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><DesiredTreble>%d</DesiredTreble>", constrain(v, -10, 10));
    rc("SetTreble", p);
  }
  bool getLoudness() {
    return tag(rc("GetLoudness", "<InstanceID>0</InstanceID><Channel>Master</Channel>"),
               "CurrentLoudness") == "1";
  }
  void setLoudness(bool on) {
    char p[96];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "<DesiredLoudness>%d</DesiredLoudness>", on ? 1 : 0);
    rc("SetLoudness", p);
  }
  // RampToVolume — gentle fade. ramp = "SLEEP_TIMER_RAMP_TYPE" | "ALARM_RAMP_TYPE" | "AUTOPLAY_RAMP_TYPE"
  void rampToVolume(int target, const char* ramp = "SLEEP_TIMER_RAMP_TYPE") {
    char p[256];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "<RampType>%s</RampType><DesiredVolume>%d</DesiredVolume>"
      "<ResetVolumeAfter>0</ResetVolumeAfter><ProgramURI></ProgramURI>",
      ramp, constrain(target, 0, 100));
    rc("RampToVolume", p);
  }

  // --- AVTransport ---
  // Transport actions return the SOAP response body. Empty string means the
  // call failed (HTTP non-200 or SOAP fault) — callers use this to flag
  // "did Sonos actually do it?" in the UI / activity log.
  String play()     { return av("Play", "<InstanceID>0</InstanceID><Speed>1</Speed>"); }
  String pause()    { return av("Pause", "<InstanceID>0</InstanceID>"); }
  String stop()     { return av("Stop", "<InstanceID>0</InstanceID>"); }
  String next()     { return av("Next", "<InstanceID>0</InstanceID>"); }
  String previous() { return av("Previous", "<InstanceID>0</InstanceID>"); }
  void setPlayMode(const char* mode) {
    char p[96];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><NewPlayMode>%s</NewPlayMode>", mode);
    av("SetPlayMode", p);
  }
  void setCrossfade(bool on) {
    char p[80];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><CrossfadeMode>%d</CrossfadeMode>", on ? 1 : 0);
    av("SetCrossfadeMode", p);
  }
  // duration: "01:00:00" or empty to cancel
  void setSleepTimer(const char* duration) {
    char p[120];
    snprintf(p, sizeof(p),
      "<InstanceID>0</InstanceID><NewSleepTimerDuration>%s</NewSleepTimerDuration>",
      duration);
    av("ConfigureSleepTimer", p);
  }

  String getTransportState() {
    String r = av("GetTransportInfo", "<InstanceID>0</InstanceID>");
    return tag(r, "CurrentTransportState");
  }

  bool isPlaying() {
    String s = getTransportState();
    s.toUpperCase();
    return s == "PLAYING" || s == "TRANSITIONING";
  }

  // Track info with metadata
  struct TrackInfo {
    String title;
    String artist;
    String album;
    String artURL;
    String duration;
    String elapsed;
  };

  static String unescape(const String& s) {
    String out = s;
    out.replace("&lt;", "<");
    out.replace("&gt;", ">");
    out.replace("&amp;", "&");
    out.replace("&quot;", "\"");
    out.replace("&apos;", "'");
    return out;
  }

  TrackInfo getPositionInfo() {
    String r = av("GetPositionInfo", "<InstanceID>0</InstanceID>");
    TrackInfo t;
    t.duration = tag(r, "TrackDuration");
    t.elapsed  = tag(r, "RelTime");

    // Metadata is HTML-escaped DIDL-Lite XML
    String meta = unescape(tag(r, "TrackMetaData"));
    if (meta.length() > 0) {
      t.title  = tag(meta, "dc:title");
      t.artist = tag(meta, "dc:creator");
      t.album  = tag(meta, "upnp:album");
      t.artURL = tag(meta, "upnp:albumArtURI");
      // Art URL may be relative — make absolute
      if (t.artURL.length() > 0 && !t.artURL.startsWith("http"))
        t.artURL = "http://" + ip + ":1400" + t.artURL;
    }
    return t;
  }
};

// =============================================================================
// Speaker state + persistence
// =============================================================================
struct SpeakerInfo {
  String ip;
  String name;
};

struct SpeakerState {
  String ip;
  String name;
  int    volume  = 0;
  bool   muted   = false;
  bool   playing = false;
  bool   online  = false;
  int    idx     = -1;
  // Now playing
  String title, artist, album, artURL, duration, elapsed;

  bool connected() const { return online && ip.length() > 0; }
};

static SonosController ctrl;
static SpeakerState spk;
static std::vector<SpeakerInfo> speakers;

static bool refreshState() {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;

  int vol = ctrl.getVolume();
  // Real SOAP failure: getVolume() returns -1 (empty response or missing tag).
  // A *value* of 0 is a legitimate "user cranked it down" — not a failure.
  if (vol < 0) {
    dbg("refresh: SOAP failure on getVolume");
    return false;
  }
  bool mute = ctrl.getMute();
  bool play = ctrl.isPlaying();

  spk.volume = vol;
  spk.muted = mute;
  spk.playing = play;

  // Track info
  auto t = ctrl.getPositionInfo();
  spk.title    = t.title;
  spk.artist   = t.artist;
  spk.album    = t.album;
  spk.artURL   = t.artURL;
  spk.duration = t.duration;
  spk.elapsed  = t.elapsed;

  dbg("state: vol=%d %s - %s", vol, t.artist.c_str(), t.title.c_str());
  return true;
}

static bool setVolume(int vol) {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;
  ctrl.setVolume(vol);
  spk.volume = constrain(vol, 0, 100);
  if (vol > 0) spk.muted = false;
  return true;
}

static int adjustVolume(int delta) {
  if (!spk.connected()) return spk.volume;
  ctrl.ip = spk.ip;
  int newVol = ctrl.setRelativeVolume(delta);
  if (newVol > 0 || delta < 0) spk.volume = newVol;
  if (newVol > 0) spk.muted = false;
  return newVol;
}

static bool toggleMute() {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;
  // Read current mute state from speaker rather than trusting local cache —
  // local can drift when other controllers (Sonos app) change it.
  bool current = ctrl.getMute();
  bool target = !current;
  String r = ctrl.setMute(target);
  if (r.length() == 0) return false;  // SOAP rejected
  bool actual = ctrl.getMute();
  spk.muted = actual;
  return actual == target;
}

static bool togglePlay() {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;
  String resp = spk.playing ? ctrl.pause() : ctrl.play();
  if (resp.length() == 0) return false;     // SOAP rejected (rare for play/pause)
  spk.playing = !spk.playing;
  return true;
}

static bool nextTrack() {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;
  // SOAP returns empty on failure — Sonos Radio + most streaming sources will
  // reject Next with err=800 (TRANSITION_NOT_AVAILABLE).
  return ctrl.next().length() > 0;
}

static bool prevTrack() {
  if (!spk.connected()) return false;
  ctrl.ip = spk.ip;
  return ctrl.previous().length() > 0;
}

static void selectSpeaker(int idx) {
  if (idx < 0 || idx >= (int)speakers.size()) return;
  spk.idx = idx;
  spk.ip = speakers[idx].ip;
  spk.name = speakers[idx].name;
  spk.online = true;
  ctrl.ip = spk.ip;

  Preferences p;
  p.begin("sonos", false);
  p.putString("ip", spk.ip);
  p.putString("name", spk.name);
  p.end();

  dbg("selected: %s @ %s", spk.name.c_str(), spk.ip.c_str());
  refreshState();
}

static void restoreSpeaker() {
  Preferences p;
  p.begin("sonos", true);
  String savedIP   = p.getString("ip", "");
  String savedName = p.getString("name", "");
  p.end();

  if (savedName.length() == 0 && savedIP.length() == 0) return;

  for (size_t i = 0; i < speakers.size(); i++)
    if (speakers[i].ip == savedIP) { selectSpeaker(i); return; }
  for (size_t i = 0; i < speakers.size(); i++)
    if (speakers[i].name.equalsIgnoreCase(savedName)) { selectSpeaker(i); return; }

  dbg("saved speaker '%s' not found", savedName.c_str());
}

static void cycleNext() {
  if (speakers.size() <= 1) return;
  selectSpeaker((spk.idx + 1) % speakers.size());
}
