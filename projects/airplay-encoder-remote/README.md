# AirPlay Encoder Remote - HomeKit Edition

A rotary encoder-based HomeKit remote control for AirPlay devices, including the Anthem MRX receiver.

## Overview

This project creates a physical HomeKit remote control using an ESP32 and a rotary encoder. Once paired with your Home app, it can control any AirPlay-compatible device on your network.

## Hardware

- **ESP32-C3-Zero** (or any ESP32)
- **Rotary Encoder** with push button
- **LED** (optional status indicator)

### GPIO Pin Mapping (ESP32-C3-Zero)

| Function | GPIO | Label |
|----------|------|-------|
| Encoder CLK | 2 | D2 |
| Encoder DT | 3 | D3 |
| Encoder Button | 6 | D6 |
| Status LED | 8 | D8 |

## Features

### Rotary Encoder
- **Rotate**: Adjust volume (5% per detent)
- Volume range: 0-100%

### Button Controls
- **Single Click**: Play/Pause
- **Double Click**: Next Track
- **Triple Click**: Previous Track
- **Long Press (700ms)**: Toggle Mute

## How It Works with Anthem MRX

Your Anthem MRX receiver supports AirPlay, which means:

1. **Direct Control**: When streaming audio directly to the Anthem via AirPlay (from iPhone, Mac, etc.), this remote can control volume and playback
2. **HomeKit Integration**: The remote appears as a "Television" accessory in the Home app with full media controls
3. **Multi-Device**: Can control any AirPlay-enabled device on your network once paired

### Typical Usage Scenarios

**Scenario 1: iPhone → Anthem MRX**
- You're playing Apple Music from your iPhone to the Anthem via AirPlay
- This remote controls the playback: volume, play/pause, skip tracks
- Works seamlessly through HomeKit

**Scenario 2: HomePod → Anthem MRX (AirPlay 2)**
- If using AirPlay 2 multi-room with HomePods
- Remote can control the entire group or individual speakers

**Scenario 3: Apple TV → Anthem MRX (HDMI)**
- Apple TV connected to Anthem via HDMI
- Remote can control Apple TV playback
- Volume control depends on HDMI-CEC setup

## Installation

### 1. Install HomeSpan Library

In Arduino IDE:
```
Tools → Manage Libraries → Search "HomeSpan" → Install
```

Or via PlatformIO:
```ini
lib_deps =
    HomeSpan
```

### 2. Configure WiFi

Edit `AirPlayEncoderRemote.ino`:
```cpp
constexpr char WIFI_SSID[] = "YourWiFiNetwork";
constexpr char WIFI_PASSWORD[] = "YourPassword";
```

### 3. Upload to ESP32

1. Select your ESP32 board in Arduino IDE
2. Connect via USB
3. Upload the sketch

### 4. Pair with Home App

**First Boot:**
1. Open Serial Monitor (115200 baud)
2. Look for the HomeKit setup code (8-digit, format: XXX-XX-XXX)
3. You'll also see a QR code to scan

**Pairing:**
1. Open Home app on iPhone/iPad
2. Tap "+" → Add Accessory
3. Either:
   - Scan the QR code from Serial Monitor, OR
   - Enter the 8-digit setup code manually
4. Follow prompts to add "Encoder Remote"

**HomeKit Setup Code:** The default setup code is `466-37-726` (can be changed in HomeSpan settings)

## Usage

Once paired:

- **In Home App**: Control appears as "Encoder Remote" with volume, play/pause, and track controls
- **Physical Controls**: Use the encoder and button as described above
- **Voice Control**: "Hey Siri, play music on Anthem" then use the physical remote
- **Automation**: Can be integrated into HomeKit scenes and automations

## Configuration

### Adjust Volume Sensitivity
```cpp
constexpr int VOLUME_STEP_PER_TICK = 5;  // Change to 1-10
```

### Invert Encoder Direction
```cpp
constexpr bool ENCODER_INVERT = true;
```

### Button Timing
```cpp
constexpr unsigned long HOLD_THRESHOLD_MS = 700;       // Long press duration
constexpr unsigned long MULTI_CLICK_TIMEOUT_MS = 500;  // Time between clicks
```

## Troubleshooting

### Can't Pair with Home App
1. Check Serial Monitor for setup code
2. Ensure ESP32 and iPhone are on same WiFi network
3. Reset HomeKit pairing: Hold button during boot

### Controls Don't Work
1. Verify the device is AirPlay-compatible and showing in Home app
2. Check that audio is actively playing via AirPlay
3. Try controlling from Home app first to verify connection

### Anthem MRX Not Responding
1. Ensure Anthem firmware is up to date (AirPlay 2 support)
2. Check AirPlay is enabled in Anthem settings
3. Verify network connectivity between ESP32 and Anthem
4. Some Anthem features require control via the source device (iPhone/Mac) rather than receiver directly

### Volume Not Changing on Anthem
- HomeKit controls the *source* volume (iPhone, Mac, HomePod)
- Anthem may need separate IR/RS232 control for receiver volume
- Consider using HDMI-CEC for volume control integration

## HomeSpan Resources

- [HomeSpan Documentation](https://github.com/HomeSpan/HomeSpan)
- [HomeSpan API Reference](https://github.com/HomeSpan/HomeSpan/blob/master/docs/Reference.md)
- [HomeKit Accessory Protocol](https://developer.apple.com/support/homekit-accessory-protocol/)

## Future Enhancements

- [ ] Add IR transmitter for direct Anthem receiver control
- [ ] Support for multiple AirPlay zones
- [ ] OLED display showing current track/volume
- [ ] Battery power with deep sleep
- [ ] Direct RAOP control for non-HomeKit AirPlay devices

## License

MIT License - feel free to modify and adapt for your use case.

## Credits

Built with [HomeSpan](https://github.com/HomeSpan/HomeSpan) - HomeKit for ESP32
