# WLED Encoder Controller - Quick Setup

## Step 1: Install Required Libraries

In Arduino IDE → Tools → Manage Libraries, install:

1. **ArduinoJson** by Benoit Blanchon
2. **ESPAsyncWebServer** by lacamera (or Me No Dev)
3. **AsyncTCP** by Me No Dev (for ESP32)

## Step 2: Configure WiFi

Edit `WLEDEncoderController.ino` lines 12-13:
```cpp
constexpr char WIFI_SSID[] = "YourNetworkName";
constexpr char WIFI_PASSWORD[] = "YourPassword";
```

## Step 3: Upload to ESP32

1. Select board: **ESP32C3 Dev Module**
2. Partition Scheme: **Huge APP (3MB No OTA)**
3. USB CDC On Boot: **Enabled**
4. Click Upload

## Step 4: Web Configuration

1. Open Serial Monitor (115200 baud)
2. Find the URL (e.g., `http://wled-controller.local`)
3. Open in browser
4. Click **"Scan for WLED Devices"**
5. Check the segments you want to control
6. Click **"Save Configuration"**

## Step 5: Done!

The encoder is now ready to control your WLED lights!

### Controls:
- **Rotate**: Brightness
- **1 Click**: Next light
- **2 Clicks**: Color mode (RGB only)
- **3 Clicks**: All-lights mode
- **Hold**: Toggle on/off

### LED Blinks:
- 1-5 blinks = Light 1-5 selected
- 2 blinks = Color mode
- 3 blinks = All-lights mode
- 5 blinks = Error

## Troubleshooting

**No WLED devices found:**
- Ensure WLED devices are on same network
- Check WLED devices have mDNS enabled (default: on)
- Try accessing WLED web interface directly

**Can't access web config:**
- Try `http://[ESP32-IP-ADDRESS]` instead of `.local`
- Check Serial Monitor for actual IP address

**Libraries missing:**
- Install all three libraries listed in Step 1
- Restart Arduino IDE after installing

Visit http://wled-controller.local anytime to reconfigure!
