#pragma once
#include <Arduino.h>

// =============================================================================
// M5Stack Unit PoE-P4 (ESP32-P4 + IP101GRI native RMII Ethernet)
// =============================================================================

// Internal RMII Ethernet PHY (IP101GRI)
// RMII data pins (28,29,30,34,35,49,50) are fixed by the EMAC peripheral
constexpr int  PHY_MDC   = 31;
constexpr int  PHY_MDIO  = 52;
constexpr int  PHY_POWER = 51;   // PHY_RST line (used as power-enable)
constexpr int  PHY_ADDR  = 1;

// Adafruit Seesaw I2C Rotary Encoder (PID 4991) on the Unit PoE-P4 Grove port.
// Saddle wiring uncertain — we try (SDA=53, SCL=54) first, and if no Seesaw
// responds, retry with the pins swapped. Whichever works gets logged.
constexpr uint8_t PIN_I2C_SDA = 53;
constexpr uint8_t PIN_I2C_SCL = 54;

// Seesaw module config — all values per Adafruit's PID 4991 example.
constexpr uint8_t  SEESAW_ADDR    = 0x36;  // default; jumperable up to 0x3D
constexpr uint8_t  SS_SWITCH      = 24;    // Seesaw GPIO carrying the encoder push-switch
constexpr uint8_t  SS_NEOPIX      = 6;     // Seesaw GPIO driving the onboard NeoPixel
constexpr uint16_t SS_EXPECT_VER  = 4991;  // product ID returned by ss.getVersion()
constexpr uint8_t  PIN_LED        = 22;    // optional GPIO status LED on Hat2-Bus

// Encoder behavior. BTN_ACTIVE/BTN_IDLE are kept so the existing button FSM
// can stay structurally identical — the Seesaw switch is active-LOW like EC11.
// Default volume change per encoder detent. Runtime value is loaded from NVS
// at boot and exposed as a slider in the web UI.
constexpr int  VOLUME_STEP_DEFAULT = 2;
constexpr int  VOLUME_STEP_MIN     = 1;
constexpr int  VOLUME_STEP_MAX     = 10;
// Default inversion at first boot — runtime value is loaded/saved in NVS by
// loadEncoderInvert()/saveEncoderInvert() so it's per-board, web-UI-toggleable.
constexpr bool ENCODER_INVERT_DEFAULT = true;
constexpr int  BTN_ACTIVE = LOW;
constexpr int  BTN_IDLE   = HIGH;

// Timing (ms)
constexpr unsigned long T_ETH_LINK     = 15000;
constexpr unsigned long T_DHCP         = 15000;
constexpr unsigned long T_DEBOUNCE     = 25;
constexpr unsigned long T_HOLD         = 700;     // short-hold trigger
constexpr unsigned long T_LONG_HOLD    = 2000;    // long-hold trigger (additional 1.3s)
constexpr unsigned long T_MULTI_CLICK  = 350;
constexpr unsigned long T_VOL_THROTTLE = 120;
constexpr unsigned long T_STATE_POLL   = 5000;
constexpr unsigned long T_REDISCOVER   = 300000;  // 5 min
constexpr unsigned long T_MODE_TIMEOUT = 5000;    // auto-exit non-default mode after 5s idle

constexpr char HOSTNAME_PREFIX[] = "sonos-p4";
constexpr bool DEBUG_LOG = true;

// ── Firmware versioning + OTA pull-update ─────────────────────────────────
// Bumped by ./release.sh. Used for boot log, /api/status, and the OTA pull
// updater's "newer than mine?" comparison against the manifest's `version`.
constexpr char FW_VERSION[] = "1.0.1";

// Public manifest hosted in the GitHub repo. raw.githubusercontent.com is
// HTTPS, CDN-cached, and requires no auth for public repos. The manifest
// itself points to a GitHub Release asset (the .bin) so binaries don't bloat
// the git history.
constexpr char UPDATE_MANIFEST_URL[] =
  "https://raw.githubusercontent.com/davidvivesprice/arduino-projects/main/projects/SonosEthRemoteP4/manifest.json";

// Updater cadence — 5 min between checks, with ±30s jitter. The manifest
// fetch is a ~300-byte HTTPS GET, so a 9-board fleet at this cadence is ~108
// requests/hr against raw.githubusercontent.com — well below any rate limit.
// Trade-off: push a release from anywhere and worst-case ~5-6 min until every
// board has it, no VPN / no LAN access required.
constexpr unsigned long T_UPDATE_CHECK    = 5UL * 60UL * 1000UL;   // 5 min
constexpr unsigned long T_UPDATE_JITTER   = 30UL * 1000UL;         // ±30s

inline void dbg(const char* fmt, ...) {
  if (!DEBUG_LOG) return;
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[DBG] %s\n", buf);
}
