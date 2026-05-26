#pragma once
// =============================================================================
// Fleet Hub reporter — POSTs a small status JSON to the central Cloudflare
// Worker every 5 minutes (piggybacked on the manifest poll in updater.h).
//
// The hub dashboard at https://sonos-fleet-hub.davidvivesprice.workers.dev/
// shows liveness + i2c + speaker state for every deployed board across all
// clients, so the user has visibility into the entire fleet from any browser.
//
// If FLEET_HUB_SECRET is empty (config_secrets.h missing at build time) the
// reporter silently skips — firmware still works, it just doesn't phone home.
// =============================================================================
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "config.h"
#include "room.h"
#include "speaker.h"
#include "encoder.h"

void logEvent(const char* fmt, ...);  // defined in webui.h

// Track lifetime counters for trends. Reset on reboot — the hub aggregates
// across time, so per-board "since boot" is the right granularity here.
// fleetRotEvents is owned by encoder.h since that's where rotation happens.
extern uint32_t fleetRotEvents;
static uint32_t fleetSoapErrors    = 0;  // SOAP failures observed
static uint32_t fleetSpkLost       = 0;  // times speaker dropped offline
static uint32_t fleetOtaFailures   = 0;  // failed OTA attempts

extern bool ssReady;

inline void fleetReport(const char* hostname, bool i2cOk, uint16_t ssPid) {
  // Reporting requires a real secret. If the build doesn't have one, skip.
  if (strlen(FLEET_HUB_SECRET) == 0) return;

  NetworkClientSecure client;
  client.setInsecure();  // hub auth comes from the shared secret, not TLS pinning
  HTTPClient http;
  http.setUserAgent(String("SonosEthRemoteP4/") + FW_VERSION);
  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  if (!http.begin(client, FLEET_HUB_URL)) {
    logEvent("fleet: http.begin failed");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Fleet-Auth", FLEET_HUB_SECRET);

  String slug   = loadRoomSlug();
  String prefix = loadHostPrefix();
  String label  = slug.length() ? String(labelForSlug(slug.c_str())) : String("");

  // Build the payload — small, schema-stable so the hub can render even if
  // future firmware adds new fields the worker doesn't know about.
  String body;
  body.reserve(512);
  body  = "{\"id\":\"";   body += hostname;
  body += "\",\"client\":\""; body += prefix;
  body += "\",\"room\":\""; body += slug;
  body += "\",\"label\":\""; body += label;
  body += "\",\"fw\":\""; body += FW_VERSION;
  body += "\",\"up\":"; body += (unsigned long)(millis() / 1000);
  body += ",\"ip\":\""; body += ETH.localIP().toString();
  body += "\",\"i2c\":"; body += i2cOk ? "true" : "false";
  body += ",\"ss_pid\":"; body += ssPid;
  body += ",\"speaker\":\""; body += spk.name;
  body += "\",\"spkOnline\":"; body += spk.online ? "true" : "false";
  body += ",\"rotEvents\":"; body += fleetRotEvents;
  body += ",\"lastRotMs\":"; body += (unsigned long)lastRotationMs;
  body += ",\"nowMs\":"; body += (unsigned long)millis();
  body += ",\"errors\":{\"soap\":"; body += fleetSoapErrors;
  body += ",\"spk_lost\":"; body += fleetSpkLost;
  body += ",\"ota\":"; body += fleetOtaFailures;
  body += "}";
  // Gesture events ring buffer — every click/multi-click/hold since the last
  // report. Hub stores them in a rolling per-board log so the pulse view
  // shows individual user interactions with timestamps.
  body += ",\"events\":[";
  for (uint8_t i = 0; i < fleetEvtCount; i++) {
    if (i > 0) body += ',';
    body += "{\"ms\":";    body += fleetEvents[i].ms;
    body += ",\"gid\":\""; body += fleetEvents[i].gid;
    body += "\",\"ok\":";  body += fleetEvents[i].ok ? "true" : "false";
    body += "}";
  }
  body += "]}";

  int code = http.POST(body);
  if (code == 200) {
    // Clear the events buffer only on confirmed delivery — if the hub is
    // unreachable we want to retry next tick with the same events.
    fleetEventsClear();
  } else {
    logEvent("fleet: POST -> %d", code);
  }
  http.end();
}
