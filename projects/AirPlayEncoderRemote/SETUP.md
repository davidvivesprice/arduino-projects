# Quick Setup Guide

## What You Need

1. **ESP32-C3-Zero** (or any ESP32 board)
2. **Rotary Encoder Module** (KY-040 or similar)
3. **USB cable** for programming
4. **iPhone/iPad** with Home app
5. **Arduino IDE** with ESP32 board support

## Step-by-Step Setup

### 1. Wire the Hardware

```
Rotary Encoder → ESP32-C3-Zero
├─ CLK → GPIO 2 (D2)
├─ DT  → GPIO 3 (D3)
├─ SW  → GPIO 6 (D6)
├─ +   → 3.3V
└─ GND → GND

Optional LED → GPIO 8 (D8) → 220Ω resistor → GND
```

### 2. Install Arduino IDE & ESP32 Support

**Arduino IDE:**
1. Download from https://www.arduino.cc/en/software
2. Install and open

**ESP32 Board Support:**
1. File → Preferences
2. Additional Board Manager URLs:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Tools → Board → Boards Manager
4. Search "esp32" → Install "esp32 by Espressif Systems"

### 3. Install HomeSpan Library

1. Tools → Manage Libraries
2. Search "HomeSpan"
3. Install latest version

### 4. Configure & Upload

**Edit the sketch:**
```cpp
// Change these lines in AirPlayEncoderRemote.ino:
constexpr char WIFI_SSID[] = "YourWiFiName";
constexpr char WIFI_PASSWORD[] = "YourWiFiPassword";
```

**Upload:**
1. Tools → Board → ESP32 Arduino → ESP32C3 Dev Module
2. Tools → Port → (select your ESP32 port)
3. Click Upload (→) button
4. Wait for "Done uploading"

### 5. Get the HomeKit Pairing Code

1. Open Tools → Serial Monitor
2. Set baud rate to 115200
3. Press reset button on ESP32
4. Look for output like:
   ```
   ╔══════════════════════════════╗
   ║  HOMEKIT SETUP CODE: 466-37-726  ║
   ╚══════════════════════════════╝
   ```
5. **Write down this code!**

### 6. Pair with Home App

**On your iPhone/iPad:**
1. Open **Home** app
2. Tap **"+"** (top right)
3. Tap **"Add Accessory"**
4. Choose **"More options..."**
5. You should see **"Encoder Remote"** appear
6. Tap it
7. Tap **"Add Anyway"** (it will say uncertified - this is normal)
8. Enter the setup code from Serial Monitor
9. Choose a room (e.g., "Living Room")
10. Tap **Done**

### 7. Test It!

**Physical Controls:**
- **Rotate encoder**: Adjust volume
- **Single click**: Play/Pause
- **Double click**: Next track
- **Triple click**: Previous track
- **Long press**: Mute/Unmute

**In Home App:**
- Open the remote control
- You'll see volume slider and playback controls
- Try it both in the app and with the physical encoder!

## Testing with Anthem MRX

1. **Start AirPlay stream**:
   - On iPhone: Control Center → Music → AirPlay icon
   - Select your Anthem MRX receiver
   - Start playing music

2. **Control with encoder**:
   - Rotate to adjust volume
   - Click to pause/play
   - Double-click for next track

## Troubleshooting

**ESP32 won't show up in port menu:**
- Install CH340 or CP2102 USB drivers
- Try a different USB cable (must be data cable, not charge-only)

**Can't find "Encoder Remote" in Home app:**
- Check Serial Monitor - ESP32 must be connected to WiFi
- Ensure iPhone and ESP32 are on same WiFi network
- Try tapping "Add Accessory" → "I Don't Have a Code" → "More options"

**Setup code not working:**
- Double-check you copied it correctly from Serial Monitor
- Try resetting: Unplug ESP32, wait 10 seconds, plug back in
- Check for typos in WiFi credentials

**Encoder not responding:**
- Check all wire connections
- Verify GPIO pins match your board
- Open Serial Monitor and watch for encoder events

## Next Steps

Once working with AirPlay:
- Add direct Anthem IP control for volume
- Add OLED display for visual feedback
- Create custom HomeKit automations
- Add IR transmitter for full receiver control

## Need Help?

Check Serial Monitor output at 115200 baud - it shows detailed debug info about what the remote is doing.
