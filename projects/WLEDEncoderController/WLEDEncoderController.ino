#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// -----------------------------------------------------------------------------
// User configuration
// -----------------------------------------------------------------------------
constexpr char WIFI_SSID[] = "Happy Valley";
constexpr char WIFI_PASSWORD[] = "welcomehome";
constexpr char HOSTNAME[] = "wled-controller";
constexpr bool WAIT_FOR_SERIAL = true;
constexpr bool DEBUG_LOGGING = true;
constexpr bool ENCODER_INVERT = false;
constexpr bool BUTTON_ACTIVE_HIGH = false;

// GPIO mapping for Waveshare ESP32-C3-Zero
namespace Pins {
constexpr uint8_t rotaryCLK = 2;   // D2
constexpr uint8_t rotaryDT = 3;    // D3
constexpr uint8_t rotarySW = 6;    // D6 (push switch)
constexpr uint8_t statusLED = 8;   // D8  (indicator LED)
}  // namespace Pins

// Behaviour tuning
constexpr int BRIGHTNESS_STEP_PER_TICK = 10;
constexpr int HUE_STEP_PER_TICK = 5;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long HOLD_THRESHOLD_MS = 700;
constexpr unsigned long MULTI_CLICK_TIMEOUT_MS = 500;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr unsigned long COMMAND_DELAY_MS = 100;
constexpr unsigned long LED_BLINK_MS = 100;

constexpr int MAX_LIGHTS = 10;

// WLED Configuration
struct WLEDLight {
  char ip[16];
  int segment;
  char name[32];
  bool isRGB;
  bool enabled;
};

WLEDLight lights[MAX_LIGHTS];
int numLights = 0;

// State management
enum ControlMode {
  MODE_BRIGHTNESS,
  MODE_COLOR,
  MODE_ALL_LIGHTS
};

struct LightState {
  int brightness = 128;
  int hue = 0;
  int saturation = 255;
  bool isOn = true;
};

LightState lightStates[MAX_LIGHTS];
int currentLight = 0;
ControlMode currentMode = MODE_BRIGHTNESS;
unsigned long lastCommand = 0;

// Button state
struct ButtonState {
  int level;
  int lastReading;
  unsigned long lastChange = 0;
  unsigned long pressStart = 0;
  unsigned long lastRelease = 0;
  bool holdHandled = false;
  uint8_t pendingClicks = 0;

  static constexpr int ACTIVE_LEVEL = BUTTON_ACTIVE_HIGH ? HIGH : LOW;
  static constexpr int IDLE_LEVEL = BUTTON_ACTIVE_HIGH ? LOW : HIGH;
} buttonState;

HTTPClient http;
AsyncWebServer server(80);
Preferences preferences;

// Forward declarations
void debugPrint(const String &msg);
void connectToWiFi();
void setupWebServer();
void scanWLEDDevices();
void loadConfiguration();
void saveConfiguration();
void processEncoder();
void processButton();
void handleClickAction(uint8_t clicks);
void handleHoldAction();
void cycleToNextLight();
void enterColorMode();
void toggleAllLightsMode();
void toggleCurrentLight();
void updateBrightness(int delta);
void updateHue(int delta);
bool sendWLEDCommand(int lightIndex, const String &json);
void blinkStatusLED(int times = 1);

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  if (WAIT_FOR_SERIAL) {
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000UL) {
      delay(10);
    }
  }

  Serial.println("\n\nWLED Encoder Controller (Web Config)");
  Serial.println("=====================================");

  // Configure hardware
  pinMode(Pins::rotaryCLK, INPUT_PULLUP);
  pinMode(Pins::rotaryDT, INPUT_PULLUP);
  pinMode(Pins::rotarySW, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(Pins::statusLED, OUTPUT);
  digitalWrite(Pins::statusLED, LOW);

  const int raw = digitalRead(Pins::rotarySW);
  buttonState.lastReading = raw;
  buttonState.level = raw;

  Serial.println("Hardware configured");

  // Connect to WiFi
  connectToWiFi();

  // Setup mDNS
  if (MDNS.begin(HOSTNAME)) {
    Serial.printf("mDNS started: http://%s.local\n", HOSTNAME);
  }

  // Load saved configuration
  loadConfiguration();

  // Setup web server
  setupWebServer();

  if (numLights == 0) {
    Serial.println("\n*** NO LIGHTS CONFIGURED ***");
    Serial.printf("Visit http://%s.local to setup\n", HOSTNAME);
    blinkStatusLED(5);
  } else {
    Serial.printf("\nConfigured %d lights - Ready!\n", numLights);
    Serial.printf("Visit http://%s.local to reconfigure\n\n", HOSTNAME);
    blinkStatusLED(1);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    connectToWiFi();
    delay(5000);
    return;
  }

  if (numLights > 0) {
    processButton();
    processEncoder();
  }

  delay(5);
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
void loadConfiguration() {
  preferences.begin("wled-ctrl", false);
  numLights = preferences.getInt("numLights", 0);

  if (numLights > 0) {
    Serial.printf("Loading %d lights from flash...\n", numLights);
    for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
      String key = String("light") + i;
      size_t len = preferences.getBytesLength(key.c_str());
      if (len == sizeof(WLEDLight)) {
        preferences.getBytes(key.c_str(), &lights[i], sizeof(WLEDLight));
        Serial.printf("  %d: %s @ %s seg:%d %s\n",
          i + 1, lights[i].name, lights[i].ip, lights[i].segment,
          lights[i].isRGB ? "(RGB)" : "(White)");
      }
    }
  }

  preferences.end();
}

void saveConfiguration() {
  preferences.begin("wled-ctrl", false);
  preferences.putInt("numLights", numLights);

  for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
    String key = String("light") + i;
    preferences.putBytes(key.c_str(), &lights[i], sizeof(WLEDLight));
  }

  preferences.end();
  Serial.printf("Saved %d lights to flash\n", numLights);
}

// -----------------------------------------------------------------------------
// Web Server
// -----------------------------------------------------------------------------
void setupWebServer() {
  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"(
<!DOCTYPE html>
<html>
<head>
  <title>WLED Controller Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #eee; }
    h1 { color: #0ff; }
    button {
      background: #0aa;
      color: white;
      border: none;
      padding: 12px 20px;
      margin: 5px;
      cursor: pointer;
      border-radius: 4px;
      font-size: 16px;
    }
    button:hover { background: #0cc; }
    button:disabled { background: #555; cursor: not-allowed; }
    .device {
      background: #2a2a2a;
      padding: 15px;
      margin: 10px 0;
      border-radius: 8px;
      border-left: 4px solid #0aa;
    }
    .segment {
      margin-left: 20px;
      padding: 8px;
      background: #333;
      margin-top: 5px;
      border-radius: 4px;
    }
    .segment input { margin-right: 8px; }
    #status {
      padding: 10px;
      margin: 10px 0;
      background: #333;
      border-radius: 4px;
    }
    .success { color: #0f0; }
    .error { color: #f66; }
    .info { color: #0cf; }
  </style>
</head>
<body>
  <h1>WLED Controller Setup</h1>
  <p>Select segments to control with the encoder:</p>

  <button onclick="scan()" id="scanBtn">Scan for WLED Devices</button>
  <button onclick="save()" id="saveBtn" disabled>Save Configuration</button>
  <button onclick="location.reload()">Refresh</button>

  <div id="status">Ready to scan...</div>
  <div id="devices"></div>

  <script>
    let selectedSegments = [];

    async function scan() {
      document.getElementById('scanBtn').disabled = true;
      document.getElementById('status').innerHTML = '<span class="info">Scanning network...</span>';
      document.getElementById('devices').innerHTML = '';

      const response = await fetch('/scan');
      const data = await response.json();

      if (data.devices && data.devices.length > 0) {
        document.getElementById('status').innerHTML =
          `<span class="success">Found ${data.devices.length} WLED device(s)</span>`;
        displayDevices(data.devices);
        document.getElementById('saveBtn').disabled = false;
      } else {
        document.getElementById('status').innerHTML =
          '<span class="error">No WLED devices found</span>';
      }

      document.getElementById('scanBtn').disabled = false;
    }

    function displayDevices(devices) {
      const container = document.getElementById('devices');
      devices.forEach(device => {
        const div = document.createElement('div');
        div.className = 'device';

        let html = `<h3>${device.name} (${device.ip})</h3>`;
        html += `<p>Brand: ${device.brand} | Version: ${device.ver}</p>`;

        device.segments.forEach(seg => {
          const id = `${device.ip}_${seg.id}`;
          html += `<div class="segment">
            <input type="checkbox" id="${id}"
              data-ip="${device.ip}"
              data-seg="${seg.id}"
              data-name="${seg.name}"
              data-rgb="${seg.isRGB}">
            <label for="${id}">
              Segment ${seg.id}: ${seg.name}
              (${seg.leds} LEDs) ${seg.isRGB ? 'RGB' : 'White'}
            </label>
          </div>`;
        });

        div.innerHTML = html;
        container.appendChild(div);
      });
    }

    async function save() {
      const checkboxes = document.querySelectorAll('input[type="checkbox"]:checked');
      const segments = Array.from(checkboxes).map(cb => ({
        ip: cb.dataset.ip,
        segment: parseInt(cb.dataset.seg),
        name: cb.dataset.name,
        isRGB: cb.dataset.rgb === 'true'
      }));

      if (segments.length === 0) {
        alert('Please select at least one segment');
        return;
      }

      if (segments.length > 10) {
        alert('Maximum 10 segments allowed');
        return;
      }

      document.getElementById('status').innerHTML = '<span class="info">Saving...</span>';

      const response = await fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({lights: segments})
      });

      if (response.ok) {
        document.getElementById('status').innerHTML =
          `<span class="success">Saved ${segments.length} segment(s)! Controller ready.</span>`;
        setTimeout(() => location.reload(), 2000);
      } else {
        document.getElementById('status').innerHTML =
          '<span class="error">Failed to save configuration</span>';
      }
    }
  </script>
</body>
</html>
)";
    request->send(200, "text/html", html);
  });

  // Scan endpoint
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(4096);
    JsonArray devices = doc.createNestedArray("devices");

    Serial.println("Starting WLED scan...");

    // Scan network for WLED devices via mDNS
    int n = MDNS.queryService("http", "tcp");
    Serial.printf("Found %d HTTP services\n", n);

    for (int i = 0; i < n; i++) {
      String hostname = MDNS.hostname(i);
      if (hostname.indexOf("wled") >= 0 || hostname.indexOf("WLED") >= 0) {
        IPAddress ip = MDNS.IP(i);
        String ipStr = ip.toString();

        Serial.printf("Checking WLED at %s...\n", ipStr.c_str());

        // Query WLED for info
        HTTPClient httpClient;
        String url = "http://" + ipStr + "/json/info";
        httpClient.begin(url);
        int code = httpClient.GET();

        if (code == HTTP_CODE_OK) {
          String payload = httpClient.getString();
          DynamicJsonDocument infoDoc(2048);
          deserializeJson(infoDoc, payload);

          JsonObject device = devices.createNestedObject();
          device["ip"] = ipStr;
          device["name"] = infoDoc["name"].as<String>();
          device["brand"] = infoDoc["brand"].as<String>();
          device["ver"] = infoDoc["ver"].as<String>();

          // Get segments
          httpClient.end();
          httpClient.begin("http://" + ipStr + "/json/state");
          code = httpClient.GET();

          if (code == HTTP_CODE_OK) {
            payload = httpClient.getString();
            DynamicJsonDocument stateDoc(4096);
            deserializeJson(stateDoc, payload);

            JsonArray segments = device.createNestedArray("segments");
            JsonArray segs = stateDoc["seg"];

            for (JsonObject seg : segs) {
              JsonObject segment = segments.createNestedObject();
              segment["id"] = seg["id"];
              segment["name"] = seg["n"] | String("Segment " + String((int)seg["id"]));
              segment["leds"] = seg["len"] | seg["stop"].as<int>() - seg["start"].as<int>();

              // Check if RGB by looking at color array
              JsonArray col = seg["col"];
              bool hasRGB = false;
              if (col.size() > 0) {
                JsonArray firstCol = col[0];
                hasRGB = firstCol.size() >= 3;
              }
              segment["isRGB"] = hasRGB;
            }
          }
        }
        httpClient.end();
      }
    }

    serializeJson(doc, *response);
    request->send(response);
  });

  // Save endpoint
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(2048);
      deserializeJson(doc, data);

      JsonArray lightsArray = doc["lights"];
      numLights = 0;

      for (JsonObject light : lightsArray) {
        if (numLights >= MAX_LIGHTS) break;

        strncpy(lights[numLights].ip, light["ip"].as<const char*>(), 15);
        lights[numLights].segment = light["segment"];
        strncpy(lights[numLights].name, light["name"].as<const char*>(), 31);
        lights[numLights].isRGB = light["isRGB"];
        lights[numLights].enabled = true;
        numLights++;
      }

      saveConfiguration();

      request->send(200, "text/plain", "OK");
    });

  server.begin();
  Serial.println("Web server started");
}

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------
void connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
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
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed!");
  }
}

// -----------------------------------------------------------------------------
// Encoder processing (same as before)
// -----------------------------------------------------------------------------
void processEncoder() {
  static const int8_t transitionTable[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                             -1, 0, 0, 1, 0, 1, -1, 0};
  static bool stateInitialised = false;
  static uint8_t lastState = 0;
  static int8_t quarterSteps = 0;

  const uint8_t clk = digitalRead(Pins::rotaryCLK);
  const uint8_t dt = digitalRead(Pins::rotaryDT);
  const uint8_t currentState = (clk << 1) | dt;

  if (!stateInitialised) {
    lastState = currentState;
    stateInitialised = true;
    return;
  }

  if (currentState == lastState) return;

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

  if (currentMode == MODE_COLOR) {
    updateHue(detentSteps * HUE_STEP_PER_TICK);
  } else {
    updateBrightness(detentSteps * BRIGHTNESS_STEP_PER_TICK);
  }
}

// Button, click handling, and light control functions remain the same...
// (Copying from previous version)

void processButton() {
  const unsigned long now = millis();
  const int raw = digitalRead(Pins::rotarySW);

  if (raw != buttonState.lastReading) {
    buttonState.lastChange = now;
    buttonState.lastReading = raw;
  }

  if ((now - buttonState.lastChange) > BUTTON_DEBOUNCE_MS && raw != buttonState.level) {
    buttonState.level = raw;
    const bool pressed = (buttonState.level == ButtonState::ACTIVE_LEVEL);

    if (pressed) {
      buttonState.pressStart = now;
      buttonState.holdHandled = false;
    } else {
      if (!buttonState.holdHandled && buttonState.pendingClicks < 3) {
        buttonState.pendingClicks++;
      }
      buttonState.lastRelease = now;
    }
  }

  const bool pressed = (buttonState.level == ButtonState::ACTIVE_LEVEL);
  if (pressed && !buttonState.holdHandled && (now - buttonState.pressStart) >= HOLD_THRESHOLD_MS) {
    buttonState.holdHandled = true;
    buttonState.pendingClicks = 0;
    handleHoldAction();
  }

  if (buttonState.level == ButtonState::IDLE_LEVEL && buttonState.pendingClicks > 0 &&
      (now - buttonState.lastRelease) > MULTI_CLICK_TIMEOUT_MS) {
    handleClickAction(buttonState.pendingClicks);
    buttonState.pendingClicks = 0;
  }
}

void handleClickAction(uint8_t clicks) {
  switch(clicks) {
    case 1: cycleToNextLight(); break;
    case 2:
      if (currentMode == MODE_COLOR) {
        currentMode = MODE_BRIGHTNESS;
        Serial.println("Exited color mode");
        blinkStatusLED(1);
      } else {
        enterColorMode();
      }
      break;
    case 3: toggleAllLightsMode(); break;
  }
}

void handleHoldAction() {
  toggleCurrentLight();
}

void cycleToNextLight() {
  currentLight = (currentLight + 1) % numLights;
  if (currentMode == MODE_COLOR && !lights[currentLight].isRGB) {
    currentMode = MODE_BRIGHTNESS;
  }
  Serial.printf("Selected: %s\n", lights[currentLight].name);
  blinkStatusLED(currentLight + 1);
}

void enterColorMode() {
  if (currentMode == MODE_ALL_LIGHTS || !lights[currentLight].isRGB) {
    blinkStatusLED(5);
    return;
  }
  currentMode = MODE_COLOR;
  Serial.println("Entered color mode");
  blinkStatusLED(2);
}

void toggleAllLightsMode() {
  if (currentMode == MODE_ALL_LIGHTS) {
    currentMode = MODE_BRIGHTNESS;
    blinkStatusLED(1);
  } else {
    currentMode = MODE_ALL_LIGHTS;
    blinkStatusLED(3);
  }
}

void updateBrightness(int delta) {
  const unsigned long now = millis();
  if (now - lastCommand < COMMAND_DELAY_MS) return;

  if (currentMode == MODE_ALL_LIGHTS) {
    for (int i = 0; i < numLights; i++) {
      lightStates[i].brightness = constrain(lightStates[i].brightness + delta, 0, 255);
      String json = String("{\"seg\":[{\"id\":") + lights[i].segment + ",\"bri\":" + lightStates[i].brightness + "}]}";
      sendWLEDCommand(i, json);
    }
  } else {
    lightStates[currentLight].brightness = constrain(lightStates[currentLight].brightness + delta, 0, 255);
    String json = String("{\"seg\":[{\"id\":") + lights[currentLight].segment + ",\"bri\":" + lightStates[currentLight].brightness + "}]}";
    sendWLEDCommand(currentLight, json);
    Serial.printf("%s brightness: %d\n", lights[currentLight].name, lightStates[currentLight].brightness);
  }
  lastCommand = now;
}

void updateHue(int delta) {
  if (!lights[currentLight].isRGB) return;

  const unsigned long now = millis();
  if (now - lastCommand < COMMAND_DELAY_MS) return;

  lightStates[currentLight].hue = (lightStates[currentLight].hue + delta + 360) % 360;

  float h = lightStates[currentLight].hue / 60.0;
  float s = 1.0;
  float v = 1.0;

  int i = floor(h);
  float f = h - i;
  float p = v * (1 - s);
  float q = v * (1 - s * f);
  float t = v * (1 - s * (1 - f));

  float r, g, b;
  switch(i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }

  String json = String("{\"seg\":[{\"id\":") + lights[currentLight].segment +
                ",\"col\":[[" + (int)(r*255) + "," + (int)(g*255) + "," + (int)(b*255) + "]]}]}";
  sendWLEDCommand(currentLight, json);
  lastCommand = now;
}

void toggleCurrentLight() {
  lightStates[currentLight].isOn = !lightStates[currentLight].isOn;
  String json = String("{\"seg\":[{\"id\":") + lights[currentLight].segment +
                ",\"on\":" + (lightStates[currentLight].isOn ? "true" : "false") + "}]}";
  sendWLEDCommand(currentLight, json);
}

bool sendWLEDCommand(int lightIndex, const String &json) {
  if (lightIndex < 0 || lightIndex >= numLights) return false;

  String url = String("http://") + lights[lightIndex].ip + "/json/state";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(json);
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

void blinkStatusLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(Pins::statusLED, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(Pins::statusLED, LOW);
    if (i < times - 1) delay(LED_BLINK_MS);
  }
}

void debugPrint(const String &msg) {
  if (DEBUG_LOGGING) {
    Serial.print("[DBG] ");
    Serial.println(msg);
  }
}
