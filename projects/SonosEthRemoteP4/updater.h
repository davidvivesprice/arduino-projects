#pragma once
// =============================================================================
// OTA pull-update from GitHub Releases.
//
// Periodically (every ~6h with jitter) the board fetches a small JSON manifest
// over HTTPS from raw.githubusercontent.com. If the manifest's `version` is
// newer than FW_VERSION, it downloads the binary listed at `url`, verifies
// SHA-256, streams into the ESP32 Update partition, and reboots.
//
// ESP32 has dual OTA partitions, so if the new firmware fails to boot, the
// bootloader rolls back to the last known-good partition automatically — we
// don't need to implement rollback ourselves.
//
// Trigger paths:
//   - Periodic timer in loop()         (auto)
//   - POST /api/checkupdate            (manual, web UI button)
// =============================================================================
#include <Arduino.h>
#include <Update.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"
#include "config.h"
#include "fleet.h"

void logEvent(const char* fmt, ...);  // defined in webui.h

// Reflects the most recent updater action — surfaced via /api/status so the
// web UI can show "checking…", "no update available", "downloading…", or an
// error message after a check.
struct UpdaterState {
  String  status     = "idle";   // idle / checking / downloading / installing / error / up_to_date
  String  latestVer  = "";       // last seen manifest version
  String  lastError  = "";
  uint32_t lastCheckMs = 0;
};
static UpdaterState updaterState;

// ── Version compare (returns >0 if a>b, <0 if a<b, 0 if equal) ────────────
inline int compareVersion(const String& a, const String& b) {
  int ax[3] = {0,0,0}, bx[3] = {0,0,0};
  sscanf(a.c_str(), "%d.%d.%d", &ax[0], &ax[1], &ax[2]);
  sscanf(b.c_str(), "%d.%d.%d", &bx[0], &bx[1], &bx[2]);
  for (int i = 0; i < 3; i++) if (ax[i] != bx[i]) return ax[i] - bx[i];
  return 0;
}

// ── Hex helpers ───────────────────────────────────────────────────────────
inline void bytesToHex(const uint8_t* in, size_t n, char* out) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i*2]   = H[(in[i] >> 4) & 0xF];
    out[i*2+1] = H[ in[i]       & 0xF];
  }
  out[n*2] = 0;
}

// ── Stream binary into Update + verify SHA-256 in the same pass ───────────
// Returns true on success. On failure, leaves updaterState.lastError set.
static bool streamUpdateWithHash(NetworkClient& stream, size_t contentLen,
                                  const String& expectedSha) {
  // Begin Update — pass contentLen so the partition is sized correctly.
  if (!Update.begin(contentLen)) {
    updaterState.lastError = String("Update.begin failed: ") + Update.errorString();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);  // 0 = SHA-256 (not SHA-224)

  const size_t BUFSZ = 1024;
  uint8_t buf[BUFSZ];
  size_t totalRead = 0;
  unsigned long lastReport = millis();

  while (totalRead < contentLen) {
    size_t want = min(BUFSZ, contentLen - totalRead);
    int n = stream.readBytes((char*)buf, want);
    if (n <= 0) {
      // Network stall — give it a moment, then bail.
      if (millis() - lastReport > 10000) {
        updaterState.lastError = "network stall during download";
        mbedtls_sha256_free(&sha);
        Update.abort();
        return false;
      }
      delay(20);
      continue;
    }
    if (Update.write(buf, n) != (size_t)n) {
      updaterState.lastError = String("Update.write failed: ") + Update.errorString();
      mbedtls_sha256_free(&sha);
      Update.abort();
      return false;
    }
    mbedtls_sha256_update(&sha, buf, n);
    totalRead += n;
    if (millis() - lastReport > 1000) {
      lastReport = millis();
      logEvent("ota %u / %u bytes (%u%%)",
        (unsigned)totalRead, (unsigned)contentLen,
        (unsigned)(totalRead * 100 / contentLen));
    }
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  char gotHex[65];
  bytesToHex(digest, 32, gotHex);
  String got = String(gotHex);
  String want = expectedSha;
  want.toLowerCase();
  got.toLowerCase();

  if (got != want) {
    updaterState.lastError = String("sha256 mismatch — expected ") + want + " got " + got;
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {  // true = finalize partition
    updaterState.lastError = String("Update.end failed: ") + Update.errorString();
    return false;
  }
  return true;
}

// ── Public: fetch manifest + apply if newer. force=true bypasses the
// "newer than mine?" check for testing/rollback. Caller is responsible for
// ensuring Ethernet is up.
inline bool checkForUpdate(bool force = false) {
  updaterState.status = "checking";
  updaterState.lastError = "";
  updaterState.lastCheckMs = millis();

  NetworkClientSecure manifestClient;
  manifestClient.setInsecure();  // sha256 of binary is the real integrity check

  HTTPClient http;
  http.setUserAgent(String("SonosEthRemoteP4/") + FW_VERSION);
  http.setTimeout(8000);
  if (!http.begin(manifestClient, UPDATE_MANIFEST_URL)) {
    updaterState.status = "error";
    updaterState.lastError = "http.begin(manifest) failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    updaterState.status = "error";
    updaterState.lastError = String("manifest HTTP ") + code;
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    updaterState.status = "error";
    updaterState.lastError = String("manifest JSON parse: ") + jerr.c_str();
    return false;
  }

  String latest = doc["version"] | "";
  String url    = doc["url"]     | "";
  String sha256 = doc["sha256"]  | "";
  updaterState.latestVer = latest;

  if (latest.length() == 0 || url.length() == 0 || sha256.length() == 0) {
    updaterState.status = "error";
    updaterState.lastError = "manifest missing version/url/sha256";
    return false;
  }

  if (!force && compareVersion(latest, FW_VERSION) <= 0) {
    updaterState.status = "up_to_date";
    logEvent("ota: up to date (have %s, manifest %s)", FW_VERSION, latest.c_str());
    return true;
  }

  logEvent("ota: %s available (currently %s) — downloading", latest.c_str(), FW_VERSION);
  updaterState.status = "downloading";

  NetworkClientSecure binClient;
  binClient.setInsecure();
  HTTPClient bin;
  bin.setUserAgent(String("SonosEthRemoteP4/") + FW_VERSION);
  bin.setTimeout(15000);
  // Follow GitHub Release asset redirects (302 → objects.githubusercontent.com).
  bin.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!bin.begin(binClient, url)) {
    updaterState.status = "error";
    updaterState.lastError = "http.begin(bin) failed";
    return false;
  }
  int bc = bin.GET();
  if (bc != 200) {
    updaterState.status = "error";
    updaterState.lastError = String("binary HTTP ") + bc;
    bin.end();
    return false;
  }
  size_t contentLen = bin.getSize();
  if (contentLen <= 0) {
    updaterState.status = "error";
    updaterState.lastError = "binary has no Content-Length";
    bin.end();
    return false;
  }
  logEvent("ota: %u bytes incoming", (unsigned)contentLen);

  updaterState.status = "installing";
  NetworkClient* stream = bin.getStreamPtr();
  bool ok = streamUpdateWithHash(*stream, contentLen, sha256);
  bin.end();

  if (!ok) {
    updaterState.status = "error";
    logEvent("ota FAILED: %s", updaterState.lastError.c_str());
    return false;
  }

  logEvent("ota: installed v%s — rebooting", latest.c_str());
  updaterState.status = "rebooting";
  delay(500);
  ESP.restart();
  return true;
}

// ── Schedule periodic checks ───────────────────────────────────────────────
// Two cadences interleave:
//   • Manifest check + heartbeat fleet report   — every T_UPDATE_CHECK + jitter
//   • Activity-triggered fleet report           — when events buffer non-empty,
//     rate-limited to once per T_FLEET_ACTIVE so a long spin doesn't spam.
inline void updaterTick(bool ethConnected, const char* hostname,
                         bool i2cOk, uint16_t ssPid) {
  if (!ethConnected) return;

  // First scheduled check 60s after boot (give DHCP/mDNS/SSDP time).
  static unsigned long nextHeartbeat = 60UL * 1000UL;
  static unsigned long lastActivityReport = 0;
  constexpr unsigned long T_FLEET_ACTIVE = 25UL * 1000UL;  // min gap between active reports

  // Activity-triggered path — flush pending events to the hub fast so the
  // dashboard pulse stays responsive to real user input.
  bool hasEvents = (fleetEvtCount > 0);
  if (hasEvents && (millis() - lastActivityReport) > T_FLEET_ACTIVE) {
    lastActivityReport = millis();
    fleetReport(hostname, i2cOk, ssPid);
    return;  // skip heartbeat this tick; we just reported
  }

  // Heartbeat path — runs even on idle boards so the dashboard knows they're
  // alive. Bundles up any not-yet-reported events as a side-effect.
  if (millis() < nextHeartbeat) return;
  long jitter = (long)random(-(long)T_UPDATE_JITTER, (long)T_UPDATE_JITTER);
  nextHeartbeat = millis() + T_UPDATE_CHECK + jitter;
  lastActivityReport = millis();
  fleetReport(hostname, i2cOk, ssPid);
  checkForUpdate(false);  // may reboot on success
}
