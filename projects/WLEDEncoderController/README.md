# WLED Encoder Controller

A single rotary encoder controller for multiple WLED instances and segments.

## Features

- **Control 5 lights** from one encoder (2 RGB PWM, 2 White PWM, 1 Addressable)
- **Brightness control** via rotation (0-255)
- **Color control** for RGB lights (hue adjustment)
- **All-lights mode** to control everything at once
- **Per-light memory** - remembers brightness and color for each light
- **Visual feedback** via status LED blinks

## Hardware

- **ESP32-C3-Zero** (or any ESP32)
- **Rotary Encoder** with push button
- **LED** for status indication

### GPIO Pin Mapping (ESP32-C3-Zero)

| Function | GPIO | Label |
|----------|------|-------|
| Encoder CLK | 2 | D2 |
| Encoder DT | 3 | D3 |
| Encoder Button | 6 | D6 |
| Status LED | 8 | D8 |

## Controls

### Brightness Mode (Default)

- **Rotate**: Adjust brightness (0-255)
- **Single Click**: Cycle through lights
  - Light 1 → 2 → 3 → 4 → 5 → 1
  - LED blinks to show which light is selected (1-5 blinks)
- **Double Click**: Enter color mode (RGB lights only)
- **Triple Click**: Enter/exit all-lights mode
- **Long Hold (700ms)**: Toggle selected light on/off

### Color Mode (RGB lights only)

- **Rotate**: Change hue (0-360°)
- **Single Click**: Exit color mode back to brightness
- LED blinks twice when entering color mode

### All-Lights Mode

- **Rotate**: Adjust brightness on ALL lights simultaneously
- **Triple Click**: Exit back to single-light mode
- LED blinks three times when entering

## Configuration

### 1. WiFi Credentials

Edit lines 11-12:
```cpp
constexpr char WIFI_SSID[] = "YourWiFiNetwork";
constexpr char WIFI_PASSWORD[] = "YourPassword";
```

### 2. WLED Instances

Edit lines 36-42 with your WLED device IPs and segments:
```cpp
WLEDLight lights[] = {
  {"192.168.1.100", 0, "RGB Strip 1", true},   // PWM RGB segment 0
  {"192.168.1.100", 1, "RGB Strip 2", true},   // PWM RGB segment 1
  {"192.168.1.100", 2, "White Strip 1", false}, // PWM White segment 2
  {"192.168.1.100", 3, "White Strip 2", false}, // PWM White segment 3
  {"192.168.1.101", 0, "Addressable", true}    // Addressable RGB
};
```

**Parameters:**
- `ip`: WLED device IP address
- `segment`: Segment ID (0-based)
- `name`: Friendly name for Serial Monitor
- `isRGB`: `true` for RGB, `false` for white-only

### 3. Behavior Tuning

Lines 24-30:
```cpp
constexpr int BRIGHTNESS_STEP_PER_TICK = 10;  // Change per encoder click
constexpr int HUE_STEP_PER_TICK = 5;          // Hue change per click
constexpr unsigned long HOLD_THRESHOLD_MS = 700;  // Long press duration
constexpr unsigned long MULTI_CLICK_TIMEOUT_MS = 500;  // Multi-click window
```

## WLED Setup

### PWM Segments (192.168.1.100)

Your first WLED instance has PWM strips configured as segments:

1. Open WLED web interface: `http://192.168.1.100`
2. Go to **Config → LED Preferences**
3. Set up 4 segments:
   - Segment 0: RGB Strip 1
   - Segment 1: RGB Strip 2
   - Segment 2: White Strip 1 (single channel)
   - Segment 3: White Strip 2 (single channel)

### Addressable LED (192.168.1.101)

Second WLED instance with addressable LEDs:
- Segment 0: All your addressable LEDs

## Installation

### 1. Install Libraries

In Arduino IDE:
```
Tools → Manage Libraries
```

Install:
- **ArduinoJson** (by Benoit Blanchon)

### 2. Upload

1. Select board: **ESP32C3 Dev Module**
2. Set **Partition Scheme**: Huge APP (3MB No OTA)
3. Set **USB CDC On Boot**: Enabled
4. Upload the sketch

### 3. Monitor

Open Serial Monitor at 115200 baud to see:
- WiFi connection status
- Current light selection
- Brightness/hue values
- Debug messages

## How It Works

### WLED JSON API

The controller sends HTTP POST requests to WLED's JSON API:

**Brightness:**
```json
POST http://192.168.1.100/json/state
{"seg":[{"id":0,"bri":128}]}
```

**Color (RGB):**
```json
POST http://192.168.1.100/json/state
{"seg":[{"id":0,"col":[[255,0,0]]}]}
```

**On/Off:**
```json
POST http://192.168.1.100/json/state
{"seg":[{"id":0,"on":true}]}
```

### State Management

Each light remembers:
- Brightness (0-255)
- Hue (0-360°) for RGB lights
- On/Off state

When you cycle between lights, their last settings are preserved.

## Status LED Feedback

The onboard LED provides visual feedback:

| Blinks | Meaning |
|--------|---------|
| 1 | Light 1 selected |
| 2 | Light 2 selected OR entered color mode |
| 3 | Light 3 selected OR entered all-lights mode |
| 4 | Light 4 selected |
| 5 | Light 5 selected OR error (e.g., color mode on white light) |

## Troubleshooting

### "WLED error 404"
- Check IP addresses in configuration
- Verify WLED is reachable: `ping 192.168.1.100`
- Check segment IDs match your WLED setup

### Encoder not responding
- Check wiring connections
- Verify GPIO pins match your board
- Open Serial Monitor and watch for debug messages

### WiFi won't connect
- Ensure SSID and password are correct
- ESP32 only works on 2.4GHz networks
- Check Serial Monitor for connection status

### Color mode doesn't work
- Only works on RGB lights (isRGB = true)
- White-only lights will show error (5 blinks)
- Make sure you're not in all-lights mode

### Brightness changes too fast/slow
- Adjust `BRIGHTNESS_STEP_PER_TICK` (line 24)
- Higher = faster changes
- Lower = finer control

## Example Serial Monitor Output

```
WLED Encoder Controller
=======================
Hardware configured
Connecting to WiFi: Happy Valley
................
Connected! IP: 192.168.4.123

Configured lights:
  1: RGB Strip 1 @ 192.168.1.100 seg:0 (RGB)
  2: RGB Strip 2 @ 192.168.1.100 seg:1 (RGB)
  3: White Strip 1 @ 192.168.1.100 seg:2 (White)
  4: White Strip 2 @ 192.168.1.100 seg:3 (White)
  5: Addressable @ 192.168.1.101 seg:0 (RGB)

=== CONTROLS ===
Rotate: Adjust brightness (0-255)
Single click: Cycle through lights
Double click: Color mode (RGB only)
Triple click: All-lights mode
Long hold: Toggle selected light on/off
================

Starting with: RGB Strip 1
RGB Strip 1 brightness: 138
RGB Strip 1 brightness: 148
Selected: RGB Strip 2 (brightness: 128, hue: 0)
Entered color mode for RGB Strip 2 (current hue: 0)
RGB Strip 2 hue: 5 (RGB: 255,21,0)
RGB Strip 2 hue: 10 (RGB: 255,42,0)
```

## Advanced Usage

### Custom Color Presets

Modify `updateHue()` to add preset colors:
```cpp
// In handleClickAction, case for preset cycling
void cycleColorPreset() {
  static int preset = 0;
  int hues[] = {0, 120, 240, 60};  // Red, Green, Blue, Yellow
  lightStates[currentLight].hue = hues[preset];
  preset = (preset + 1) % 4;
  updateHue(0);  // Apply the color
}
```

### WLED Effects

Add effect control for addressable LEDs:
```cpp
String json = String("{\"seg\":[{\"id\":") + lights[currentLight].segment +
              ",\"fx\":1}]}";  // Effect ID 1
sendWLEDCommand(currentLight, json);
```

Check WLED documentation for effect IDs.

## Comparison to Other Projects

### vs Sonos Encoder
- ✅ **Works immediately** - WLED has HTTP API
- ✅ **No discovery needed** - Direct IP addressing
- ✅ **Lighter code** - No SOAP, simpler API
- ✅ **Visual feedback** - LED blinks show state

### vs AirPlay/HomeKit Encoder (Abandoned)
- ✅ **Actually controls devices** - Direct API calls
- ✅ **Simple and reliable** - No pairing hell
- ✅ **10x less code** - No HomeSpan library
- ✅ **Works as expected** - Does what it says

## Future Enhancements

- [ ] Save/load presets to ESP32 flash
- [ ] OLED display showing current light and values
- [ ] WLED effect control for addressable LEDs
- [ ] Sync with WLED state on startup
- [ ] Web interface for configuration
- [ ] MQTT support for home automation integration

## Credits

Built with proven encoder logic from SonosEncoderRemote project.
Uses WLED JSON API for reliable light control.

## License

MIT License - modify and use freely.
