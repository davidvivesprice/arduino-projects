#include <Arduino.h>
#include <HomeSpan.h>

// -----------------------------------------------------------------------------
// User configuration
// -----------------------------------------------------------------------------
constexpr char WIFI_SSID[] = "Happy Valley";
constexpr char WIFI_PASSWORD[] = "welcomehome";
constexpr char DEVICE_NAME[] = "Encoder Remote";
constexpr char HOSTNAME[] = "encoder-remote";  // Shows up in LanScan, router, etc.
constexpr bool WAIT_FOR_SERIAL = true;
constexpr bool DEBUG_LOGGING = true;
constexpr bool ENCODER_INVERT = false;
constexpr bool BUTTON_ACTIVE_HIGH = false;

// GPIO mapping for Waveshare ESP32-C3-Zero
namespace Pins {
constexpr uint8_t rotaryCLK = 2;   // D2
constexpr uint8_t rotaryDT = 3;    // D3
constexpr uint8_t rotarySW = 6;    // D6 (push switch)
constexpr uint8_t statusLED = 8;   // D8  (optional indicator LED)
}  // namespace Pins

// Behaviour tuning
constexpr int VOLUME_STEP_PER_TICK = 5;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long HOLD_THRESHOLD_MS = 700;
constexpr unsigned long MULTI_CLICK_TIMEOUT_MS = 500;

// -----------------------------------------------------------------------------
// Global state for encoder and button
// -----------------------------------------------------------------------------
struct EncoderState {
  int buttonLevel;
  int lastButtonReading;
  unsigned long lastButtonChange = 0;
  unsigned long pressStart = 0;
  unsigned long lastRelease = 0;
  bool holdHandled = false;
  uint8_t pendingClicks = 0;

  static const int BUTTON_ACTIVE_LEVEL = BUTTON_ACTIVE_HIGH ? HIGH : LOW;
  static const int BUTTON_IDLE_LEVEL = BUTTON_ACTIVE_HIGH ? LOW : HIGH;
} encoderState;

// -----------------------------------------------------------------------------
// HomeKit Television Speaker Service (for Volume Control)
// -----------------------------------------------------------------------------
struct DEV_TelevisionSpeaker : Service::TelevisionSpeaker {
  SpanCharacteristic *volume;
  SpanCharacteristic *mute;
  SpanCharacteristic *volumeSelector;

  DEV_TelevisionSpeaker() : Service::TelevisionSpeaker() {
    Serial.println("Configuring Television Speaker");

    volume = new Characteristic::Volume(50);
    volume->setRange(0, 100, 1);

    mute = new Characteristic::Mute(false);

    volumeSelector = new Characteristic::VolumeSelector();
  }

  boolean update() override {
    if (volumeSelector->updated()) {
      int direction = volumeSelector->getNewVal();
      int currentVol = volume->getVal();

      if (direction == 0) {  // Volume Up
        currentVol = min(100, currentVol + VOLUME_STEP_PER_TICK);
        Serial.printf("Volume Up -> %d\n", currentVol);
      } else {  // Volume Down
        currentVol = max(0, currentVol - VOLUME_STEP_PER_TICK);
        Serial.printf("Volume Down -> %d\n", currentVol);
      }

      volume->setVal(currentVol);
    }

    if (mute->updated()) {
      Serial.printf("Mute -> %s\n", mute->getNewVal() ? "ON" : "OFF");
    }

    return true;
  }
};

// -----------------------------------------------------------------------------
// HomeKit Remote Key Service (for Playback Control)
// -----------------------------------------------------------------------------
struct DEV_RemoteKey : Service::InputSource {
  SpanCharacteristic *remoteKey;

  DEV_RemoteKey() : Service::InputSource() {
    Serial.println("Configuring Remote Key");

    new Characteristic::Identifier(1);  // REQUIRED for InputSource
    new Characteristic::ConfiguredName("Media Remote");
    new Characteristic::InputSourceType(0);  // Other
    new Characteristic::IsConfigured(1);
    new Characteristic::CurrentVisibilityState(0);

    remoteKey = new Characteristic::RemoteKey();
  }

  boolean update() override {
    if (remoteKey->updated()) {
      int key = remoteKey->getNewVal();

      switch(key) {
        case 11:  // Play/Pause
          Serial.println("Remote: Play/Pause");
          break;
        case 9:   // Next Track
          Serial.println("Remote: Next Track");
          break;
        case 10:  // Previous Track
          Serial.println("Remote: Previous Track");
          break;
        default:
          Serial.printf("Remote: Key %d\n", key);
          break;
      }
    }
    return true;
  }
};

// Global reference to our services for external control
DEV_TelevisionSpeaker *tvSpeaker = nullptr;
DEV_RemoteKey *remoteKey = nullptr;

// -----------------------------------------------------------------------------
// Encoder and button processing
// -----------------------------------------------------------------------------
void processEncoder() {
  static const int8_t transitionTable[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                             -1, 0, 0, 1, 0, 1, -1, 0};
  static bool stateInitialised = false;
  static uint8_t lastState = 0;
  static int8_t quarterSteps = 0;
  static int16_t pendingDetents = 0;

  const uint8_t clk = digitalRead(Pins::rotaryCLK);
  const uint8_t dt = digitalRead(Pins::rotaryDT);
  const uint8_t currentState = (clk << 1) | dt;

  if (!stateInitialised) {
    lastState = currentState;
    stateInitialised = true;
    return;
  }

  if (currentState == lastState) {
    return;
  }

  const uint8_t index = (lastState << 2) | currentState;
  lastState = currentState;

  int8_t delta = transitionTable[index];
  if (delta == 0) return;
  if (ENCODER_INVERT) delta = -delta;

  quarterSteps += delta;

  int detentSteps = 0;
  while (quarterSteps >= 4) {
    detentSteps++;
    quarterSteps -= 4;
  }
  while (quarterSteps <= -4) {
    detentSteps--;
    quarterSteps += 4;
  }

  if (detentSteps == 0) return;

  // Apply volume change via HomeKit
  if (tvSpeaker) {
    int currentVol = tvSpeaker->volume->getVal();
    currentVol += (detentSteps * VOLUME_STEP_PER_TICK);
    currentVol = constrain(currentVol, 0, 100);
    tvSpeaker->volume->setVal(currentVol);

    Serial.printf("Encoder -> Volume: %d\n", currentVol);
  }
}

void processButton() {
  const unsigned long now = millis();
  const int raw = digitalRead(Pins::rotarySW);

  if (raw != encoderState.lastButtonReading) {
    encoderState.lastButtonChange = now;
    encoderState.lastButtonReading = raw;
  }

  if ((now - encoderState.lastButtonChange) > BUTTON_DEBOUNCE_MS &&
      raw != encoderState.buttonLevel) {
    encoderState.buttonLevel = raw;
    const bool pressed = (encoderState.buttonLevel == EncoderState::BUTTON_ACTIVE_LEVEL);

    if (pressed) {
      encoderState.pressStart = now;
      encoderState.holdHandled = false;
      Serial.println("Button pressed");
    } else {
      if (!encoderState.holdHandled && encoderState.pendingClicks < 3) {
        encoderState.pendingClicks++;
      }
      encoderState.lastRelease = now;
      Serial.printf("Button released, pending clicks: %d\n", encoderState.pendingClicks);
    }
  }

  const bool pressed = (encoderState.buttonLevel == EncoderState::BUTTON_ACTIVE_LEVEL);
  if (pressed && !encoderState.holdHandled &&
      (now - encoderState.pressStart) >= HOLD_THRESHOLD_MS) {
    encoderState.holdHandled = true;
    encoderState.pendingClicks = 0;
    Serial.println("Button hold detected -> Toggle Mute");

    // Toggle mute via HomeKit
    if (tvSpeaker) {
      bool currentMute = tvSpeaker->mute->getVal();
      tvSpeaker->mute->setVal(!currentMute);
    }
  }

  if (encoderState.buttonLevel == EncoderState::BUTTON_IDLE_LEVEL &&
      encoderState.pendingClicks > 0 &&
      (now - encoderState.lastRelease) > MULTI_CLICK_TIMEOUT_MS) {

    Serial.printf("Processing %d click(s)\n", encoderState.pendingClicks);

    if (remoteKey) {
      switch(encoderState.pendingClicks) {
        case 1:  // Single click: Play/Pause
          Serial.println("Action: Play/Pause");
          remoteKey->remoteKey->setVal(11);
          break;
        case 2:  // Double click: Next Track
          Serial.println("Action: Next Track");
          remoteKey->remoteKey->setVal(9);
          break;
        case 3:  // Triple click: Previous Track
          Serial.println("Action: Previous Track");
          remoteKey->remoteKey->setVal(10);
          break;
      }
    }

    encoderState.pendingClicks = 0;
  }
}

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

  Serial.println("\n\nAirPlay Encoder Remote - HomeKit Edition");
  Serial.println("========================================");

  // Configure hardware
  pinMode(Pins::rotaryCLK, INPUT_PULLUP);
  pinMode(Pins::rotaryDT, INPUT_PULLUP);
  pinMode(Pins::rotarySW, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(Pins::statusLED, OUTPUT);

  const int raw = digitalRead(Pins::rotarySW);
  encoderState.lastButtonReading = raw;
  encoderState.buttonLevel = raw;

  Serial.println("Hardware configured");

  // Set WiFi hostname (shows in LanScan, router, etc.)
  WiFi.setHostname(HOSTNAME);

  // Initialize HomeSpan
  homeSpan.setLogLevel(0);  // 0=minimal spam, 1=normal, 2=verbose
  homeSpan.begin(Category::Television, DEVICE_NAME);
  homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASSWORD);

  // Factory reset if button held during boot
  homeSpan.setControlPin(Pins::rotarySW);  // Press during boot to reset pairing

  // HomeSpan automatically handles mDNS as [HOSTNAME].local

  // Create HomeKit Accessory
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("DIY");
      new Characteristic::Model("EncoderRemote-v1");
      new Characteristic::Name(DEVICE_NAME);
      new Characteristic::SerialNumber("ESP32-001");
      new Characteristic::FirmwareRevision("1.0");

    SpanService *tv = new Service::Television();
      new Characteristic::Active(1);
      new Characteristic::ActiveIdentifier(1);
      new Characteristic::ConfiguredName(DEVICE_NAME);
      new Characteristic::SleepDiscoveryMode(1);
      new Characteristic::RemoteKey();

    tvSpeaker = new DEV_TelevisionSpeaker();
    tvSpeaker->addLink(tv);  // REQUIRED: Link speaker to TV

    remoteKey = new DEV_RemoteKey();
    remoteKey->addLink(tv);  // REQUIRED: Link input source to TV

  Serial.println("\n\n");
  Serial.println("========================================================");
  Serial.println("       HOMEKIT PAIRING CODE - WRITE THIS DOWN!");
  Serial.println("========================================================");
  Serial.println("");
  Serial.println("              Setup Code: 466-37-726");
  Serial.println("");
  Serial.println("========================================================");
  Serial.println("  1. Open Home app on iPhone");
  Serial.println("  2. Tap + (top right) -> Add Accessory");
  Serial.println("  3. Tap 'More options...'");
  Serial.println("  4. Select 'Encoder Remote'");
  Serial.println("  5. Enter code: 466-37-726");
  Serial.println("========================================================");
  Serial.print("  Network Hostname: ");
  Serial.print(HOSTNAME);
  Serial.println(" (for LanScan, router, etc.)");
  Serial.print("  mDNS: ");
  Serial.print(HOSTNAME);
  Serial.println(".local");
  Serial.println("========================================================");
  Serial.println("");
  Serial.println("  TROUBLESHOOTING:");
  Serial.println("  - If pairing fails: Type 'E' in Serial Monitor to erase");
  Serial.println("  - Or hold encoder button during boot for factory reset");
  Serial.println("");
  Serial.println("========================================================");
  Serial.println("\n");
}

void loop() {
  homeSpan.poll();

  processButton();
  processEncoder();

  delay(5);
}
