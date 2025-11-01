#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Sonos.h>

// -----------------------------------------------------------------------------
// User configuration
// -----------------------------------------------------------------------------
constexpr char WIFI_SSID[] = "Happy Valley";
constexpr char WIFI_PASSWORD[] = "welcomehome";
constexpr char TARGET_SPEAKER_NAME[] = "HomeLab";
constexpr bool WAIT_FOR_SERIAL = true;   // set false for headless boot
constexpr bool DEBUG_LOGGING = true;     // set false to silence debug traces
constexpr bool ENCODER_INVERT = false;         // flip rotary direction without rewiring
constexpr bool BUTTON_ACTIVE_HIGH = false;     // false = switch shorts to GND when pressed
constexpr int BUTTON_ACTIVE_LEVEL = BUTTON_ACTIVE_HIGH ? HIGH : LOW;
constexpr int BUTTON_IDLE_LEVEL = BUTTON_ACTIVE_HIGH ? LOW : HIGH;

// GPIO mapping for Waveshare ESP32-C3-Zero (matches Dx silkscreen labels)
namespace Pins {
constexpr uint8_t rotaryCLK = 2;   // D2
constexpr uint8_t rotaryDT = 3;    // D3
constexpr uint8_t rotarySW = 6;    // D6 (push switch) - GPIO4 is strapping pin, GPIO10 had pull-up issues
constexpr uint8_t statusLED = 8;   // D8  (optional indicator LED)
}  // namespace Pins

// Behaviour tuning ------------------------------------------------------------
constexpr int VOLUME_STEP_PER_TICK = 2;             // volume points per encoder detent
constexpr int MIN_VOLUME = 0;
constexpr int MAX_VOLUME = 100;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long HOLD_THRESHOLD_MS = 700;
constexpr unsigned long MULTI_CLICK_TIMEOUT_MS = 350;
constexpr unsigned long VOLUME_COMMAND_DELAY_MS = 120;  // throttle Sonos HTTP spam
constexpr unsigned long STATE_REFRESH_INTERVAL_MS = 5000UL;

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
Sonos sonos;
String targetSonosIP;
bool speakerOnline = false;

HTTPClient soapClient;

int currentVolume = 40;
bool isMuted = false;
bool isPlaying = false;

int buttonLevel = BUTTON_IDLE_LEVEL;        // debounced logical level
int lastButtonReading = BUTTON_IDLE_LEVEL;  // last raw sample
unsigned long lastButtonChange = 0;
unsigned long pressStart = 0;
unsigned long lastRelease = 0;
bool holdHandled = false;
uint8_t pendingClicks = 0;

unsigned long lastVolumeCommand = 0;
unsigned long lastStateRefresh = 0;
unsigned long lastWiFiRetry = 0;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void debugPrint(const String &msg);
void logLine(const __FlashStringHelper *msg);

void configureHardware();
void connectToWiFi();
bool initSonos();
bool discoverSpeaker();
bool pullSpeakerState();

bool updateVolumeState();
bool updateMuteState();
bool updatePlaybackState();

bool setVolumeRemote(int volume);
bool setMuting(bool mute);
bool setPlayback(bool play);

bool sendSoap(const char *service, const char *action, const char *body, String &response);

void processEncoder();
void processButton();
void handleClickAction(uint8_t clicks);

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  if (WAIT_FOR_SERIAL) {
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000UL) {
      delay(10);
    }
  }
  logLine(F("\nSingle Encoder Sonos Remote – ESP32-C3-Zero"));
  debugPrint(F("Boot sequence start"));

  configureHardware();
  debugPrint(F("Hardware configured"));

  connectToWiFi();

  if (initSonos() && discoverSpeaker()) {
    debugPrint(F("Initial Sonos state fetch"));
    pullSpeakerState();
  }
}

void loop() {
  processButton();
  processEncoder();

  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWiFiRetry >= WIFI_RETRY_INTERVAL_MS) {
      logLine(F("WiFi dropped – retrying"));
      connectToWiFi();
      lastWiFiRetry = now;
      speakerOnline = false;
      targetSonosIP.clear();
      debugPrint(F("Speaker context cleared after WiFi retry"));
    }
    delay(5);
    return;
  }

  if (!speakerOnline) {
    if (initSonos() && discoverSpeaker()) {
      debugPrint(F("Speaker rediscovered, refreshing state"));
      pullSpeakerState();
    }
  } else if (now - lastStateRefresh >= STATE_REFRESH_INTERVAL_MS) {
    debugPrint(F("Periodic state refresh"));
    pullSpeakerState();
  }

  delay(5);
}

// -----------------------------------------------------------------------------
// Hardware / Wi-Fi helpers
// -----------------------------------------------------------------------------
void configureHardware() {
  pinMode(Pins::rotaryCLK, INPUT_PULLUP);
  pinMode(Pins::rotaryDT, INPUT_PULLUP);
  pinMode(Pins::rotarySW, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(Pins::statusLED, OUTPUT);
  digitalWrite(Pins::statusLED, LOW);

  debugPrint(String(F("GPIO configured; button mode -> ")) +
             (BUTTON_ACTIVE_HIGH ? F("active-high (pulldown)") : F("active-low (pullup)")));

  const int raw = digitalRead(Pins::rotarySW);
  lastButtonReading = raw;
  buttonLevel = raw;
  debugPrint(String(F("Initial button level -> ")) + (raw == HIGH ? F("HIGH") : F("LOW")));
}

void connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  logLine(F("Connecting to WiFi..."));
  WiFi.disconnect(true);  // Clear any previous WiFi state
  WiFi.mode(WIFI_OFF);    // Fully reset WiFi
  delay(100);             // Let WiFi stack reset
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected, IP = "));
    Serial.println(WiFi.localIP());
    digitalWrite(Pins::statusLED, HIGH);
    debugPrint(String(F("WiFi up, address = ")) + WiFi.localIP().toString());
  } else {
    logLine(F("WiFi connect timeout"));
    digitalWrite(Pins::statusLED, LOW);
    debugPrint(F("WiFi connection attempt failed"));
  }
}

// -----------------------------------------------------------------------------
// Sonos discovery & state sync
// -----------------------------------------------------------------------------
bool initSonos() {
  if (sonos.isInitialized()) {
    debugPrint(F("Sonos already initialised"));
    return true;
  }

  const SonosResult result = sonos.begin();
  if (result != SonosResult::SUCCESS) {
    Serial.println(String(F("Sonos init failed: ")) + sonos.getErrorString(result));
    debugPrint(String(F("Sonos.begin() failed -> ")) + sonos.getErrorString(result));
    return false;
  }

  logLine(F("Sonos library ready"));
  debugPrint(F("Sonos.begin() succeeded"));
  return true;
}

bool discoverSpeaker() {
  const SonosResult result = sonos.discoverDevices();
  if (result != SonosResult::SUCCESS) {
    Serial.println(String(F("Discovery failed: ")) + sonos.getErrorString(result));
    debugPrint(F("Device discovery failed"));
    return false;
  }

  const auto devices = sonos.getDiscoveredDevices();
  logLine(F("Discovered Sonos devices:"));
  for (size_t i = 0; i < devices.size(); ++i) {
    Serial.print(F("  "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(devices[i].name);
    Serial.print(F(" @ "));
    Serial.println(devices[i].ip);
  }

  if (auto *device = sonos.getDeviceByName(String(TARGET_SPEAKER_NAME))) {
    targetSonosIP = device->ip;
    speakerOnline = true;
    Serial.print(F("Using speaker "));
    Serial.print(TARGET_SPEAKER_NAME);
    Serial.print(F(" @ "));
    Serial.println(targetSonosIP);
    debugPrint(String(F("Locked to speaker -> ")) + targetSonosIP);
  } else {
    Serial.println(String(F("Speaker not found: ")) + TARGET_SPEAKER_NAME);
    targetSonosIP.clear();
    speakerOnline = false;
    debugPrint(F("Speaker discovery did not find target"));
  }

  return speakerOnline;
}

bool pullSpeakerState() {
  if (!speakerOnline) {
    debugPrint(F("pullSpeakerState aborted (speaker offline)"));
    return false;
  }

  bool ok = updateVolumeState();
  ok &= updateMuteState();
  ok &= updatePlaybackState();

  if (ok) {
    Serial.print(F("State -> volume: "));
    Serial.print(currentVolume);
    Serial.print(F(" | mute: "));
    Serial.print(isMuted ? F("on") : F("off"));
    Serial.print(F(" | playing: "));
    Serial.println(isPlaying ? F("yes") : F("no"));
    lastStateRefresh = millis();
    debugPrint(F("State refresh complete"));
  } else {
    logLine(F("State refresh incomplete"));
    debugPrint(F("State refresh reported failure"));
  }

  return ok;
}

// -----------------------------------------------------------------------------
// State fetch helpers
// -----------------------------------------------------------------------------
bool updateVolumeState() {
  int volume = currentVolume;
  const SonosResult result = sonos.getVolume(targetSonosIP, volume);
  if (result == SonosResult::SUCCESS) {
    currentVolume = constrain(volume, MIN_VOLUME, MAX_VOLUME);
    debugPrint(String(F("Volume state from speaker -> ")) + currentVolume);
    return true;
  }

  Serial.println(String(F("getVolume failed: ")) + sonos.getErrorString(result));
  debugPrint(F("getVolume error"));
  return false;
}

bool updateMuteState() {
  String response;
  static const char *body =
      "<u:GetMute xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "</u:GetMute>";

  if (sendSoap("RenderingControl", "GetMute", body, response)) {
    const int start = response.indexOf(F("<CurrentMute>"));
    if (start >= 0) {
      const int end = response.indexOf(F("</CurrentMute>"), start);
      if (end > start) {
        isMuted = response.substring(start + 13, end).toInt() != 0;
        debugPrint(String(F("Mute state from speaker -> ")) + (isMuted ? F("on") : F("off")));
        return true;
      }
    }
  }
  logLine(F("GetMute failed"));
  debugPrint(F("GetMute SOAP parse failed"));
  return false;
}

bool updatePlaybackState() {
  String response;
  static const char *body =
      "<u:GetTransportInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
      "<InstanceID>0</InstanceID>"
      "</u:GetTransportInfo>";

  if (sendSoap("AVTransport", "GetTransportInfo", body, response)) {
    const int start = response.indexOf(F("<CurrentTransportState>"));
    if (start >= 0) {
      const int end = response.indexOf(F("</CurrentTransportState>"), start);
      if (end > start) {
        String state = response.substring(start + 23, end);
        state.toUpperCase();
        isPlaying = (state == F("PLAYING") || state == F("TRANSITIONING"));
        debugPrint(String(F("Playback state from speaker -> ")) + state);
        return true;
      }
    }
  }
  logLine(F("GetTransportInfo failed"));
  debugPrint(F("GetTransportInfo SOAP parse failed"));
  return false;
}

// -----------------------------------------------------------------------------
// Command helpers
// -----------------------------------------------------------------------------
bool setVolumeRemote(int volume) {
  volume = constrain(volume, MIN_VOLUME, MAX_VOLUME);
  if (volume == currentVolume) {
    debugPrint(F("setVolumeRemote skipped (no change)"));
    return true;
  }

  const SonosResult result = sonos.setVolume(targetSonosIP, volume);
  if (result == SonosResult::SUCCESS) {
    currentVolume = volume;
    if (currentVolume > 0) {
      isMuted = false;  // Sonos unmutes automatically when volume > 0
    }
    lastStateRefresh = millis();
    Serial.print(F("Volume set -> "));
    Serial.println(currentVolume);
    debugPrint(String(F("setVolumeRemote success -> ")) + currentVolume);
    return true;
  }

  Serial.println(String(F("setVolume failed: ")) + sonos.getErrorString(result));
  debugPrint(String(F("setVolumeRemote failed -> ")) + sonos.getErrorString(result));
  return false;
}

bool setMuting(bool mute) {
  const SonosResult result = sonos.setMute(targetSonosIP, mute);
  if (result == SonosResult::SUCCESS) {
    debugPrint(String(F("setMuting success -> ")) + (mute ? F("on") : F("off")));
    updateMuteState();
    Serial.println(mute ? F("Muted") : F("Unmuted"));
    return true;
  }
  Serial.println(String(F("setMute failed: ")) + sonos.getErrorString(result));
  debugPrint(String(F("setMuting failed -> ")) + sonos.getErrorString(result));
  return false;
}

bool setPlayback(bool play) {
  const SonosResult result = play ? sonos.play(targetSonosIP) : sonos.pause(targetSonosIP);
  if (result == SonosResult::SUCCESS) {
    debugPrint(String(F("setPlayback success -> ")) + (play ? F("play") : F("pause")));
    updatePlaybackState();
    Serial.println(play ? F("Play") : F("Pause"));
    return true;
  }
  Serial.println(String(F("Playback change failed: ")) + sonos.getErrorString(result));
  debugPrint(String(F("setPlayback failed -> ")) + sonos.getErrorString(result));
  return false;
}

// -----------------------------------------------------------------------------
// SOAP helper (for mute / state queries Sonos lib does not expose)
// -----------------------------------------------------------------------------
bool sendSoap(const char *service, const char *action, const char *body, String &response) {
  if (!speakerOnline) {
    debugPrint(String(F("sendSoap aborted (offline) for action ")) + action);
    return false;
  }

  String url = "http://" + targetSonosIP + ":1400/MediaRenderer/" + service + "/Control";
  String envelope =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body>";
  envelope += body;
  envelope += F("</s:Body></s:Envelope>");

  soapClient.end();
  soapClient.begin(url);
  soapClient.addHeader("Content-Type", "text/xml; charset=utf-8");
  soapClient.addHeader("SOAPAction",
                       "\"urn:schemas-upnp-org:service:" + String(service) + ":1#" + action + "\"");

  const int httpCode = soapClient.POST(envelope);
  if (httpCode == HTTP_CODE_OK) {
    response = soapClient.getString();
    soapClient.end();
    debugPrint(String(F("SOAP success -> ")) + action);
    return true;
  }

  soapClient.end();
  Serial.print(F("SOAP ")); Serial.print(action); Serial.print(F(" HTTP error: ")); Serial.println(httpCode);
  debugPrint(String(F("SOAP error -> ")) + action + F(" code ") + httpCode);
  return false;
}

// -----------------------------------------------------------------------------
// Rotary encoder processing (polled quadrature decoder)
// -----------------------------------------------------------------------------
void processEncoder() {
  static const int8_t transitionTable[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                             -1, 0, 0, 1, 0, 1, -1, 0};
  static bool stateInitialised = false;
  static uint8_t lastState = 0;
  static int8_t quarterSteps = 0;   // counts quarter-detents (4 per click)
  static int16_t pendingDetents = 0; // detents waiting for volume command

  const uint8_t clk = digitalRead(Pins::rotaryCLK);
  const uint8_t dt = digitalRead(Pins::rotaryDT);
  const uint8_t currentState = (clk << 1) | dt;

  if (!stateInitialised) {
    lastState = currentState;
    stateInitialised = true;
    debugPrint(String(F("Encoder initialised -> state ")) + currentState);
    return;
  }

  if (currentState == lastState) {
    return;
  }

  const uint8_t index = (lastState << 2) | currentState;
  lastState = currentState;

  int8_t delta = transitionTable[index];
  if (delta == 0) {
    return;  // ignore bounce / invalid transitions
  }
  if (ENCODER_INVERT) {
    delta = -delta;
  }

  quarterSteps += delta;
  debugPrint(String(F("Quarter-step delta -> ")) + delta + F(" cumulative -> ") + quarterSteps);

  int detentSteps = 0;
  while (quarterSteps >= 4) {
    detentSteps++;
    quarterSteps -= 4;
  }
  while (quarterSteps <= -4) {
    detentSteps--;
    quarterSteps += 4;
  }

  if (detentSteps == 0) {
    return;
  }

  pendingDetents += detentSteps;
  debugPrint(String(F("Pending detents -> ")) + pendingDetents);

  if (!speakerOnline) {
    debugPrint(F("Encoder movement ignored (speaker offline)"));
    return;
  }

  const unsigned long now = millis();
  if (now - lastVolumeCommand < VOLUME_COMMAND_DELAY_MS) {
    return;  // wait for throttle window to expire
  }

  lastVolumeCommand = now;
  const int volumeDelta = pendingDetents * VOLUME_STEP_PER_TICK;
  pendingDetents = 0;

  if (volumeDelta == 0) {
    return;
  }

  debugPrint(String(F("Applying volume delta -> ")) + volumeDelta);
  setVolumeRemote(currentVolume + volumeDelta);
}

// -----------------------------------------------------------------------------
// Button / gesture processing
// -----------------------------------------------------------------------------
void processButton() {
  const unsigned long now = millis();
  const int raw = digitalRead(Pins::rotarySW);

  if (raw != lastButtonReading) {
    lastButtonChange = now;
    lastButtonReading = raw;
    debugPrint(String(F("Button edge detected -> ")) + (raw == HIGH ? F("HIGH") : F("LOW")));
  }

  if ((now - lastButtonChange) > BUTTON_DEBOUNCE_MS && raw != buttonLevel) {
    buttonLevel = raw;
    const bool pressed = (buttonLevel == BUTTON_ACTIVE_LEVEL);
    if (pressed) {
      pressStart = now;
      holdHandled = false;
      debugPrint(F("Button pressed"));
    } else {
      if (!holdHandled && pendingClicks < 3) {
        pendingClicks++;
      }
      lastRelease = now;
      debugPrint(String(F("Button released, pending clicks -> ")) + pendingClicks);
    }
  }

  const bool pressed = (buttonLevel == BUTTON_ACTIVE_LEVEL);
  if (pressed && !holdHandled && (now - pressStart) >= HOLD_THRESHOLD_MS) {
    holdHandled = true;
    pendingClicks = 0;
    debugPrint(F("Button hold detected"));
    handleClickAction(0xFF);
  }

  if (buttonLevel == BUTTON_IDLE_LEVEL && pendingClicks > 0 &&
      (now - lastRelease) > MULTI_CLICK_TIMEOUT_MS) {
    debugPrint(String(F("Dispatching clicks -> ")) + pendingClicks);
    handleClickAction(pendingClicks);
    pendingClicks = 0;
  }
}

void handleClickAction(uint8_t clicks) {
  if (!speakerOnline) {
    logLine(F("No speaker selected"));
    debugPrint(F("Action ignored (speaker offline)"));
    return;
  }

  switch (clicks) {
    case 1:
      debugPrint(F("Action: single click (toggle play/pause)"));
      setPlayback(!isPlaying);
      break;
    case 2:
      debugPrint(F("Action: double click (next track)"));
      if (sonos.next(targetSonosIP) == SonosResult::SUCCESS) {
        logLine(F("Next track"));
        updatePlaybackState();
      } else {
        logLine(F("Next failed"));
        updatePlaybackState();
      }
      break;
    case 3:
      debugPrint(F("Action: triple click (previous track)"));
      if (sonos.previous(targetSonosIP) == SonosResult::SUCCESS) {
        logLine(F("Previous track"));
        updatePlaybackState();
      } else {
        logLine(F("Previous failed"));
        updatePlaybackState();
      }
      break;
    case 0xFF:
      debugPrint(F("Action: hold (toggle mute)"));
      setMuting(!isMuted);
      break;
    default:
      debugPrint(String(F("Action: unhandled click count -> ")) + clicks);
      break;
  }
}

// -----------------------------------------------------------------------------
// Logging helpers
// -----------------------------------------------------------------------------
void logLine(const __FlashStringHelper *msg) {
  Serial.println(msg);
}

void debugPrint(const String &msg) {
  if (!DEBUG_LOGGING) {
    return;
  }
  Serial.print(F("[DBG] "));
  Serial.println(msg);
}
