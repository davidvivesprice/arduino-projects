#include <Arduino.h>
#include <WiFi.h>
#include <esp32_sonos.h>

// ---- WiFi Configuration ----
const char *WIFI_SSID = "Happy Valley";
const char *WIFI_PASSWORD = "welcomehome";

// ---- Sonos Configuration ----
const char *TARGET_SPEAKER_NAME = "HomeLab";
const int MAX_SONOS_DEVICES = 10;

// ---- Hardware Configuration ----
const uint8_t ROTARY_PIN_A = 13;
const uint8_t ROTARY_PIN_B = 10;
const uint8_t ROTARY_BUTTON_PIN = 14;

// ---- Timing Constants (milliseconds) ----
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long BUTTON_DEBOUNCE_MS = 25;
const unsigned long HOLD_THRESHOLD_MS = 700;
const unsigned long MULTI_CLICK_TIMEOUT_MS = 500;  // Increased from 350 to allow for triple-click
const unsigned long STATE_REFRESH_INTERVAL_MS = 5000;
const unsigned long DISCOVERY_RETRY_INTERVAL_MS = 10000;
const unsigned long VOLUME_COMMAND_DELAY_MS = 120;

// ---- Volume Configuration ----
const int VOLUME_STEP_PER_TICK = 2;  // Change to 1 for finer control
const int MIN_VOLUME = 0;
const int MAX_VOLUME = 100;

// ---- Globals ----
WiFiClient networkClient;
void ethConnectError();
SonosUPnP sonosClient(networkClient, ethConnectError);

IPAddress targetSonosIP(0, 0, 0, 0);

volatile int16_t encoderStepBuffer = 0;
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
int pendingVolumeSteps = 0;

int currentVolume = 0;
bool isMuted = false;
bool isPlaying = false;

bool buttonState = HIGH;
bool lastButtonReading = HIGH;
unsigned long lastButtonChange = 0;
unsigned long pressStart = 0;
unsigned long lastRelease = 0;
bool holdActionHandled = false;
uint8_t queuedClicks = 0;

unsigned long lastVolumeCommand = 0;
unsigned long lastStateRefresh = 0;
unsigned long lastDiscoveryAttempt = 0;
unsigned long lastWiFiRetry = 0;

// ---- Function Prototypes ----
void connectToWiFi();
bool discoverTargetSonos();
bool hasTargetSonos();
void refreshSonosState();
void requestStateRefreshSoon();
void processEncoder();
void handleButton();
void onSingleClick();
void onDoubleClick();
void onTripleClick();
void onHold();

void IRAM_ATTR encoderISR() {
  static uint32_t lastMicros = 0;
  uint32_t now = micros();
  if (now - lastMicros < 400) {
    return;
  }
  lastMicros = now;

  const int8_t direction = digitalRead(ROTARY_PIN_B) ? -1 : 1;
  portENTER_CRITICAL_ISR(&encoderMux);
  encoderStepBuffer += direction;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Single Encoder Sonos Controller"));

  pinMode(ROTARY_PIN_A, INPUT_PULLUP);
  pinMode(ROTARY_PIN_B, INPUT_PULLUP);
  pinMode(ROTARY_BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), encoderISR, CHANGE);

  connectToWiFi();
  if (discoverTargetSonos()) {
    refreshSonosState();
  }
}

void loop() {
  handleButton();
  processEncoder();

  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWiFiRetry > 5000) {
      Serial.println(F("Reconnecting WiFi..."));
      connectToWiFi();
      lastWiFiRetry = now;
      targetSonosIP = IPAddress(0, 0, 0, 0);
    }
    delay(5);
    return;
  }

  if (!hasTargetSonos() && now - lastDiscoveryAttempt > DISCOVERY_RETRY_INTERVAL_MS) {
    lastDiscoveryAttempt = now;
    if (discoverTargetSonos()) {
      refreshSonosState();
    }
  }

  if (hasTargetSonos() && (now - lastStateRefresh) > STATE_REFRESH_INTERVAL_MS) {
    refreshSonosState();
  }

  delay(5);
}

void connectToWiFi() {
  Serial.print(F("Connecting to WiFi: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi connection failed."));
  }
}

bool discoverTargetSonos() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  IPAddress discovered[MAX_SONOS_DEVICES];
  const uint8_t count = sonosClient.CheckUPnP(discovered, MAX_SONOS_DEVICES);
  bool found = false;

  Serial.print(F("Scanning for Sonos players ("));
  Serial.print(count);
  Serial.println(F(" found)"));

  for (uint8_t i = 0; i < count; ++i) {
    char zoneName[48] = {0};
    if (sonosClient.getZone(discovered[i], zoneName)) {
      Serial.print(F("  - "));
      Serial.print(zoneName);
      Serial.print(F(" @ "));
      Serial.println(discovered[i]);

      if (strcmp(zoneName, TARGET_SPEAKER_NAME) == 0) {
        targetSonosIP = discovered[i];
        found = true;
        break;
      }
    }
  }

  if (found) {
    Serial.print(F("Locked on Sonos speaker "));
    Serial.print(TARGET_SPEAKER_NAME);
    Serial.print(F(" @ "));
    Serial.println(targetSonosIP);
  } else {
    Serial.print(F("Target speaker "));
    Serial.print(TARGET_SPEAKER_NAME);
    Serial.println(F(" not found."));
    targetSonosIP = IPAddress(0, 0, 0, 0);
  }

  return found;
}

bool hasTargetSonos() {
  return targetSonosIP != IPAddress(0, 0, 0, 0);
}

void refreshSonosState() {
  if (!hasTargetSonos()) {
    return;
  }

  currentVolume = sonosClient.getVolume(targetSonosIP);
  currentVolume = constrain(currentVolume, MIN_VOLUME, MAX_VOLUME);
  isMuted = sonosClient.getMute(targetSonosIP);

  const uint8_t state = sonosClient.getState(targetSonosIP);
  isPlaying = (state == SONOS_STATE_PLAYING) || (state == SONOS_STATE_TRANSISTION);

  lastStateRefresh = millis();

  Serial.print(F("State -> volume: "));
  Serial.print(currentVolume);
  Serial.print(F(", mute: "));
  Serial.print(isMuted ? F("on") : F("off"));
  Serial.print(F(", playing: "));
  Serial.println(isPlaying ? F("yes") : F("no"));
}

void requestStateRefreshSoon() {
  lastStateRefresh = 0;
}

void processEncoder() {
  int16_t steps = 0;
  portENTER_CRITICAL(&encoderMux);
  steps = encoderStepBuffer;
  encoderStepBuffer = 0;
  portEXIT_CRITICAL(&encoderMux);

  if (steps != 0) {
    pendingVolumeSteps += steps;
  }

  if (!hasTargetSonos()) {
    pendingVolumeSteps = 0;
    return;
  }

  const unsigned long now = millis();
  if (pendingVolumeSteps != 0 && (now - lastVolumeCommand) > VOLUME_COMMAND_DELAY_MS) {
    const int delta = pendingVolumeSteps * VOLUME_STEP_PER_TICK;
    pendingVolumeSteps = 0;

    const int newVolume = constrain(currentVolume + delta, MIN_VOLUME, MAX_VOLUME);
    if (newVolume != currentVolume) {
      sonosClient.setVolume(targetSonosIP, newVolume);
      sonosClient.setMute(targetSonosIP, false);
      currentVolume = newVolume;
      isMuted = false;
      requestStateRefreshSoon();

      Serial.print(F("Volume set to "));
      Serial.println(currentVolume);
    }
    lastVolumeCommand = now;
  }
}

void handleButton() {
  const unsigned long now = millis();
  const bool reading = digitalRead(ROTARY_BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonChange = now;
  }

  if ((now - lastButtonChange) > BUTTON_DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        pressStart = now;
        holdActionHandled = false;
      } else {
        if (!holdActionHandled) {
          queuedClicks++;
          lastRelease = now;
          Serial.print(F("Click detected, count: "));
          Serial.println(queuedClicks);
        }
      }
    }
  }

  if ((buttonState == LOW) && !holdActionHandled && (now - pressStart) >= HOLD_THRESHOLD_MS) {
    holdActionHandled = true;
    queuedClicks = 0;
    onHold();
  }

  if (queuedClicks > 0 && (now - lastRelease) > MULTI_CLICK_TIMEOUT_MS) {
    Serial.print(F("Processing "));
    Serial.print(queuedClicks);
    Serial.println(F(" click(s)"));
    if (queuedClicks == 1) {
      onSingleClick();
    } else if (queuedClicks == 2) {
      onDoubleClick();
    } else {
      onTripleClick();
    }
    queuedClicks = 0;
  }

  lastButtonReading = reading;
}

void onSingleClick() {
  if (!hasTargetSonos()) {
    return;
  }
  sonosClient.togglePause(targetSonosIP);
  requestStateRefreshSoon();
  Serial.println(F("Toggle play/pause"));
}

void onDoubleClick() {
  if (!hasTargetSonos()) {
    return;
  }
  sonosClient.skip(targetSonosIP, SONOS_DIRECTION_FORWARD);
  requestStateRefreshSoon();
  Serial.println(F("Next track"));
}

void onTripleClick() {
  if (!hasTargetSonos()) {
    return;
  }
  sonosClient.skip(targetSonosIP, SONOS_DIRECTION_BACKWARD);
  requestStateRefreshSoon();
  Serial.println(F("Previous track"));
}

void onHold() {
  if (!hasTargetSonos()) {
    return;
  }
  sonosClient.toggleMute(targetSonosIP);
  requestStateRefreshSoon();
  Serial.println(F("Toggle mute"));
}

void ethConnectError() {
  Serial.println(F("Sonos network error"));
}
