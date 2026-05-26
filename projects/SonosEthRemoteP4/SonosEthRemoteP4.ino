/*
 * SonosEthRemoteP4 — Wired Sonos Controller (ESP32-P4 build)
 * M5Stack Unit PoE-P4 + Adafruit Seesaw I2C Rotary Encoder (PID 4991)
 * on the Grove HY2.0 port (G53=SDA, G54=SCL).
 *
 * Rotate=Volume | Click=Play/Pause | 2x=Next | 3x=Prev | 4x=Cycle | Hold=Mute
 * Web UI at http://<hostname>.local/   OTA enabled.
 *
 * Native RMII Ethernet (IP101GRI). Direct SOAP/UPnP control — no libs.
 */

#include <ArduinoOTA.h>
#include <esp_mac.h>
#include <Wire.h>
#include "Adafruit_seesaw.h"
#include "seesaw_neopixel.h"
#include "config.h"
#include "room.h"
#include "ethernet.h"
#include "speaker.h"
#include "modes.h"
#include "discovery.h"
#include "encoder.h"
#include "updater.h"
#include "webui.h"

// Shared with encoder.h (which declares them extern).
Adafruit_seesaw ss;
bool ssReady = false;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);
bool encoderInvert = ENCODER_INVERT_DEFAULT;  // loaded from NVS in setup()
int  volumeStep    = VOLUME_STEP_DEFAULT;     // loaded from NVS in setup()

static char hostname[24];
static unsigned long lastRefresh = 0;
static unsigned long lastDiscover = 0;
// Reflects whether ETH came up in setup() so loop() can skip ETH-only paths.
static bool ethConnected = false;

// Scan the current I2C bus and log every responding address. Useful for
// diagnosing wrong-pin / bad-cable situations from the web log.
static int scanI2C(char* outBuf, size_t outLen) {
  int found = 0;
  size_t off = snprintf(outBuf, outLen, "i2c scan:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      off += snprintf(outBuf + off, outLen - off, " 0x%02X", a);
      found++;
    }
  }
  if (found == 0) snprintf(outBuf + off, outLen - off, " (none)");
  return found;
}

// Try Seesaw init on a given SDA/SCL pair. Returns true on success.
static bool trySeesawOn(uint8_t sda, uint8_t scl) {
  Wire.end();
  Wire.begin(sda, scl);
  Wire.setClock(100000);  // stick to 100 kHz — saddle may lack pullups
  delay(20);

  char scanLog[160];
  int devices = scanI2C(scanLog, sizeof(scanLog));
  Serial.printf("SDA=%u SCL=%u  %s\n", sda, scl, scanLog);
  logEvent("SDA=%u SCL=%u  %s", sda, scl, scanLog);

  if (devices == 0) return false;
  if (!ss.begin(SEESAW_ADDR) || !sspixel.begin(SEESAW_ADDR)) {
    Serial.printf("Seesaw not at 0x%02X (devices were on bus)\n", SEESAW_ADDR);
    return false;
  }
  uint32_t v = (ss.getVersion() >> 16) & 0xFFFF;
  Serial.printf("Seesaw OK — PID %u (expecting %u)\n", v, SS_EXPECT_VER);
  logEvent("seesaw OK pid=%u sda=%u scl=%u", v, sda, scl);
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);
  (void)ss.getEncoderDelta();
  sspixel.setBrightness(0);
  sspixel.show();
  return true;
}

static bool initSeesaw() {
  // First try the configured orientation, then the swap.
  if (trySeesawOn(PIN_I2C_SDA, PIN_I2C_SCL)) return true;
  Serial.println("Retrying with SDA/SCL swapped...");
  if (trySeesawOn(PIN_I2C_SCL, PIN_I2C_SDA)) return true;
  logEvent("seesaw NOT FOUND on either pin orientation");
  return false;
}

// Hot-swap detection. Probes the I2C bus every ~2s and either re-initializes
// the Seesaw when an encoder is freshly plugged in, or marks it unhealthy
// after several consecutive ACK failures (encoder unplugged / cable yanked).
// Cheap — one bus transaction every 2 seconds, no impact on rotation reads.
static void encoderHotswapTick() {
  static unsigned long lastProbeMs   = 0;
  static int           probeFailRun  = 0;
  if (millis() - lastProbeMs < 2000) return;
  lastProbeMs = millis();

  Wire.beginTransmission(SEESAW_ADDR);
  bool present = (Wire.endTransmission() == 0);

  if (present) {
    probeFailRun = 0;
    if (!ssReady) {
      // Quick re-init (no full scan log — we already know which pins worked).
      if (ss.begin(SEESAW_ADDR) && sspixel.begin(SEESAW_ADDR)) {
        ss.pinMode(SS_SWITCH, INPUT_PULLUP);
        (void)ss.getEncoderDelta();
        sspixel.setBrightness(0);
        sspixel.show();
        ssReady = true;
        logEvent("encoder hot-plugged");
      }
    }
  } else if (ssReady) {
    // Require 3 misses in a row before flagging — single transient I2C
    // hiccups don't count.
    if (++probeFailRun >= 3) {
      ssReady = false;
      logEvent("encoder lost (i2c ACK failed 3x)");
    }
  }
}

// Accept "ROOM:<slug>\n" over USB serial → write NVS, restart.
// Called both during the boot serial window and inside loop() so that
// reassignment works whether or not Ethernet is up.
static String serialBuf;
static void handleSerialLine(const String& line) {
  if (line.startsWith("ROOM:")) {
    String slug = line.substring(5);
    slug.trim();
    slug.toLowerCase();
    if (slug.length() == 0 || isValidRoomSlug(slug)) {
      saveRoomSlug(slug);
      Serial.printf("OK room='%s' — restarting\n", slug.c_str());
      delay(250);
      ESP.restart();
    } else {
      Serial.printf("ERR invalid slug '%s' (lowercase a-z 0-9 -, ≤16 chars)\n", slug.c_str());
    }
  } else if (line.startsWith("SETUP:")) {
    // SETUP:<prefix>:<slug> — atomically sets both, reboots once.
    // Use when commissioning a board for a new client (e.g. SETUP:phil:liv).
    String rest = line.substring(6);
    int colon = rest.indexOf(':');
    if (colon < 0) {
      Serial.println("ERR SETUP format: SETUP:<prefix>:<slug>");
      return;
    }
    String pfx  = rest.substring(0, colon);
    String slug = rest.substring(colon + 1);
    pfx.trim();  pfx.toLowerCase();
    slug.trim(); slug.toLowerCase();
    bool pOk = (pfx.length()  == 0 || isValidRoomSlug(pfx));
    bool sOk = (slug.length() == 0 || isValidRoomSlug(slug));
    if (pOk && sOk) {
      saveHostPrefix(pfx);
      saveRoomSlug(slug);
      Serial.printf("OK prefix='%s' slug='%s' — restarting\n", pfx.c_str(), slug.c_str());
      delay(250);
      ESP.restart();
    } else {
      Serial.printf("ERR invalid prefix or slug (a-z 0-9 -, ≤16 chars each)\n");
    }
  } else if (line.startsWith("PREFIX:")) {
    String pfx = line.substring(7);
    pfx.trim();
    pfx.toLowerCase();
    if (pfx.length() == 0 || isValidRoomSlug(pfx)) {
      saveHostPrefix(pfx);
      Serial.printf("OK prefix='%s' — restarting\n", pfx.c_str());
      delay(250);
      ESP.restart();
    } else {
      Serial.printf("ERR invalid prefix '%s'\n", pfx.c_str());
    }
  } else if (line == "WHOAMI") {
    Serial.printf("hostname=%s prefix='%s' slug='%s'\n",
      hostname, loadHostPrefix().c_str(), loadRoomSlug().c_str());
  }
}
static void pollSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuf.length() > 0) handleSerialLine(serialBuf);
      serialBuf = "";
    } else if (serialBuf.length() < 64) {
      serialBuf += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Hostname: room-derived ("tpsvc-<slug>") if a room is assigned in NVS,
  // otherwise fall back to MAC-derived ("sonos-p4-<MAC4>") so out-of-box
  // boards are still discoverable for first-time assignment.
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  computeHostname(hostname, sizeof(hostname), mac);

  Serial.printf("\n=== %s ===\n", hostname);
  Serial.println("(send 'ROOM:<slug>' on USB serial any time to (re)assign)");

  // Status LED on Hat2-Bus (optional, leave as-is for visual heartbeat).
  pinMode(PIN_LED, OUTPUT);

  // Seed PRNG (used by updaterTick jitter). Mix MAC + boot count for entropy.
  randomSeed((uint32_t)mac[3] << 24 | (uint32_t)mac[4] << 16 | (uint32_t)mac[5] << 8 | esp_random() & 0xFF);

  Serial.printf("firmware version: %s\n", FW_VERSION);

  // Load per-board encoder direction + step from NVS.
  encoderInvert = loadEncoderInvert();
  volumeStep    = loadVolumeStep();
  Serial.printf("encoderInvert=%d  volumeStep=%d\n", encoderInvert ? 1 : 0, volumeStep);

  // Adafruit Seesaw I2C encoder on Grove. We don't halt on failure — the
  // web UI still works without the knob, so leave it recoverable.
  ssReady = initSeesaw();
  if (ssReady) {
    btn.level = btn.lastRaw =
      ss.digitalRead(SS_SWITCH) ? BTN_IDLE : BTN_ACTIVE;
  }

  // Ethernet — non-fatal. If no link, we still enter loop() and keep polling
  // serial for ROOM:<slug> commands, so a USB-only board can be assigned at
  // any time. The web UI / OTA stay dark until ETH comes up, but the device
  // is recoverable instead of halted.
  bool ethOk = initEthernet(hostname);
  if (ethOk) {
    digitalWrite(PIN_LED, HIGH);
    logEvent("eth up: %s", ETH.localIP().toString().c_str());
  } else {
    Serial.println("No Ethernet — running USB-serial-only (ROOM:<slug> still works)");
  }

  // Web UI + OTA — only if ETH is up. Without ETH the WebServer would still
  // bind but nothing would reach it, and mDNS would no-op.
  if (ethOk) {
    initWebUI(hostname);
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.begin();
    logEvent("web: http://%s.local/", hostname);

    // Discover and restore speaker
    logEvent("discovering speakers...");
    discoverSpeakers();
    restoreSpeaker();
    if (!spk.online && !speakers.empty()) {
      selectSpeaker(0);
      logEvent("auto-selected: %s", spk.name.c_str());
    }
    lastDiscover = millis();
  }
  ethConnected = ethOk;
}

void loop() {
  // Serial command poll runs unconditionally so ROOM:<slug> always works.
  pollSerialCommands();
  encoderHotswapTick();  // detect plug/unplug, auto-init on appear
  if (ethConnected) {
    ArduinoOTA.handle();
    web.handleClient();
  }
  processEncoder();
  processButton();
  modeTick();

  unsigned long now = millis();

  // ETH-dependent paths only when the link is up.
  if (ethConnected) {
    if (scanRequested) {
      scanRequested = false;
      logEvent("discovery requested");
      discoverSpeakers();
      restoreSpeaker();
      lastDiscover = now;
    }
    if (spk.connected() && (now - lastRefresh) >= T_STATE_POLL) {
      lastRefresh = now;
      static int refreshFails = 0;
      if (!refreshState()) {
        refreshFails++;
        dbg("refresh failure %d/3", refreshFails);
        // Require 3 consecutive failures (~15s) before declaring the speaker
        // offline — single transient SOAP timeouts during rapid-rotation
        // bursts shouldn't kick us into rediscovery.
        if (refreshFails >= 3) {
          spk.online = false;
          logEvent("speaker unreachable (3 failed refreshes)");
          refreshFails = 0;
        }
      } else {
        refreshFails = 0;
      }
    }
    if (!spk.connected() && (now - lastDiscover) >= T_REDISCOVER) {
      lastDiscover = now;
      logEvent("auto-rediscovery");
      discoverSpeakers();
      restoreSpeaker();
    }
    updaterTick(ethConnected, hostname, ssReady, ssReady ? SS_EXPECT_VER : 0);
  }

  delay(2);
}
