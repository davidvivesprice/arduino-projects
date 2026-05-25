#pragma once
#include <Arduino.h>
#include <Preferences.h>

// =============================================================================
// Room assignment — drives the device's mDNS hostname.
//
// When a room slug is stored in NVS, the hostname becomes "tpsvc-<slug>".
// When no room is stored (out-of-box / freshly soldered board), the hostname
// falls back to "sonos-p4-<MAC4>" so we can still find the board to assign it.
//
// The 7 canonical rooms are presented as a dropdown in the web UI; the user
// can also enter a custom slug for future installs / extra zones.
// =============================================================================

struct Room {
  const char* slug;    // hostname suffix — lowercase, [a-z0-9-] only
  const char* label;   // human-readable name shown in UI dropdown
};

constexpr Room ROOMS[] = {
  { "mstbed",  "Master Bedroom"          },
  { "mstbth",  "Master Bathroom"         },
  { "grkit",   "Great Room / Kitchen"    },
  { "grliv",   "Great Room / Living"     },
  { "fam",     "Family Room (Basement)"  },
  { "bed2",    "Bedroom 2"               },
  { "bed3",    "Bedroom 3"               },
};
constexpr size_t ROOMS_COUNT = sizeof(ROOMS) / sizeof(ROOMS[0]);

constexpr char ROOM_PREFIX[]    = "tpsvc";
constexpr char ROOM_NVS_NS[]    = "room";
constexpr char ROOM_NVS_KEY[]   = "slug";

// Validate a slug: lowercase letters, digits, hyphens only. Length 1..16.
inline bool isValidRoomSlug(const String& s) {
  if (s.length() == 0 || s.length() > 16) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

// Load the stored room slug, or empty String if unassigned.
inline String loadRoomSlug() {
  Preferences p;
  if (!p.begin(ROOM_NVS_NS, true)) return String();
  String s = p.getString(ROOM_NVS_KEY, "");
  p.end();
  return s;
}

// Save a room slug. Pass empty String to clear and fall back to MAC hostname.
inline bool saveRoomSlug(const String& slug) {
  Preferences p;
  if (!p.begin(ROOM_NVS_NS, false)) return false;
  if (slug.length() == 0) {
    p.remove(ROOM_NVS_KEY);
  } else {
    if (!isValidRoomSlug(slug)) { p.end(); return false; }
    p.putString(ROOM_NVS_KEY, slug);
  }
  p.end();
  return true;
}

// Compute the final hostname given current MAC + stored room.
// `out` must be at least 24 bytes.
inline void computeHostname(char* out, size_t outLen, const uint8_t mac[6]) {
  String slug = loadRoomSlug();
  if (slug.length() > 0) {
    snprintf(out, outLen, "%s-%s", ROOM_PREFIX, slug.c_str());
  } else {
    snprintf(out, outLen, "sonos-p4-%02x%02x", mac[4], mac[5]);
  }
}

// Look up the label for a known slug; returns "" if it's a custom/unknown one.
inline const char* labelForSlug(const char* slug) {
  for (size_t i = 0; i < ROOMS_COUNT; i++) {
    if (strcmp(ROOMS[i].slug, slug) == 0) return ROOMS[i].label;
  }
  return "";
}
