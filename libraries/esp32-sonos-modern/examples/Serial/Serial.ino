/*
 * Sonos Serial Control Example
 * 
 * Control Sonos speakers via serial commands.
 * 
 * Commands:
 * - play    : Start playback
 * - pause   : Pause playback  
 * - stop    : Stop playback
 * - next    : Skip to next track
 * - prev    : Go to previous track
 * - vol+    : Increase volume by 5
 * - vol-    : Decrease volume by 5
 * - mute    : Mute the speaker
 * - unmute  : Unmute the speaker
 * - list    : List all discovered devices
 * - select  : Select device by number
 * - help    : Show available commands
 */

#include <WiFi.h>
#include <Sonos.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// Sonos library instance
Sonos sonos;

// Current device for control (defaults to first discovered)
String currentDeviceIP = "";
String currentDeviceName = "";

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n==================================");
    Serial.println("       Sonos Serial Control         ");
    Serial.println("==================================\n");
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    
    // Initialize Sonos library
    Serial.println("\nInitializing Sonos library...");
    SonosResult result = sonos.begin();
    if (result != SonosResult::SUCCESS) {
        Serial.println("Failed to initialize Sonos: " + sonos.getErrorString(result));
        Serial.println("Please check your network connection and restart.");
        return;
    }
    
    // Discover Sonos devices
    Serial.println("\nSearching for Sonos speakers...");
    result = sonos.discoverDevices();
    if (result != SonosResult::SUCCESS) {
        Serial.println("Discovery failed: " + sonos.getErrorString(result));
        return;
    }
    
    // Display discovered devices
    displayDevices();
    
    // Select first device as default
    auto devices = sonos.getDiscoveredDevices();
    if (devices.size() > 0) {
        currentDeviceIP = devices[0].ip;
        currentDeviceName = devices[0].name;
        Serial.println("\nSelected device: " + currentDeviceName + " (" + currentDeviceIP + ")");
        
        // Get and display current volume
        int volume;
        if (sonos.getVolume(currentDeviceIP, volume) == SonosResult::SUCCESS) {
            Serial.println("Current volume: " + String(volume));
        }
    } else {
        Serial.println("\nNo Sonos devices found!");
        Serial.println("Please ensure your Sonos speakers are powered on and connected to the same network.");
    }
    
    // Show help
    showHelp();
}

void loop() {
    // Check for serial input
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toLowerCase();
        
        processCommand(command);
    }
    
    // Periodic device discovery (every 60 seconds)
    static unsigned long lastDiscovery = millis();
    if (millis() - lastDiscovery > 60000) {
        Serial.println("\nRefreshing device list...");
        sonos.discoverDevices();
        lastDiscovery = millis();
        
        // If no device is currently selected and we found devices, select the first one
        if (currentDeviceIP.length() == 0) {
            auto devices = sonos.getDiscoveredDevices();
            if (devices.size() > 0) {
                currentDeviceIP = devices[0].ip;
                currentDeviceName = devices[0].name;
                Serial.println("Auto-selected device: " + currentDeviceName + " (" + currentDeviceIP + ")");
            }
        }
    }
    
    delay(10);
}

void processCommand(const String& command) {
    // Check if we have a device selected
    if (currentDeviceIP.length() == 0 && command != "list" && command != "help") {
        Serial.println("No device selected. Use 'list' to see available devices.");
        return;
    }
    
    Serial.println("\n> " + command);
    
    if (command == "play") {
        SonosResult result = sonos.play(currentDeviceIP);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Playback started");
        } else {
            Serial.println("Play failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "pause") {
        SonosResult result = sonos.pause(currentDeviceIP);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Playback paused");
        } else {
            Serial.println("Pause failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "stop") {
        SonosResult result = sonos.stop(currentDeviceIP);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Playback stopped");
        } else {
            Serial.println("Stop failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "next") {
        SonosResult result = sonos.next(currentDeviceIP);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Skipped to next track");
        } else {
            Serial.println("Next failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "prev") {
        SonosResult result = sonos.previous(currentDeviceIP);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Returned to previous track");
        } else {
            Serial.println("Previous failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "vol+") {
        int currentVolume;
        if (sonos.getVolume(currentDeviceIP, currentVolume) == SonosResult::SUCCESS) {
            int newVolume = min(100, currentVolume + 5);
            if (sonos.setVolume(currentDeviceIP, newVolume) == SonosResult::SUCCESS) {
                Serial.println("Volume increased to " + String(newVolume));
            } else {
                Serial.println("Volume increase failed");
            }
        }
        
    } else if (command == "vol-") {
        int currentVolume;
        if (sonos.getVolume(currentDeviceIP, currentVolume) == SonosResult::SUCCESS) {
            int newVolume = max(0, currentVolume - 5);
            if (sonos.setVolume(currentDeviceIP, newVolume) == SonosResult::SUCCESS) {
                Serial.println("Volume decreased to " + String(newVolume));
            } else {
                Serial.println("Volume decrease failed");
            }
        }
        
    } else if (command == "mute") {
        SonosResult result = sonos.setMute(currentDeviceIP, true);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Speaker muted");
        } else {
            Serial.println("Mute failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "unmute") {
        SonosResult result = sonos.setMute(currentDeviceIP, false);
        if (result == SonosResult::SUCCESS) {
            Serial.println("Speaker unmuted");
        } else {
            Serial.println("Unmute failed: " + sonos.getErrorString(result));
        }
        
    } else if (command == "list") {
        displayDevices();
        
    } else if (command == "help" || command == "?") {
        showHelp();
        
    } else if (command.startsWith("select ")) {
        // Select a different device by number
        int deviceNum = command.substring(7).toInt();
        auto devices = sonos.getDiscoveredDevices();
        
        if (deviceNum > 0 && deviceNum <= devices.size()) {
            currentDeviceIP = devices[deviceNum - 1].ip;
            currentDeviceName = devices[deviceNum - 1].name;
            Serial.println("Selected device: " + currentDeviceName + " (" + currentDeviceIP + ")");
            
            // Get current volume for new device
            int volume;
            if (sonos.getVolume(currentDeviceIP, volume) == SonosResult::SUCCESS) {
                Serial.println("Current volume: " + String(volume));
            }
        } else {
            Serial.println("Invalid device number. Use 'list' to see available devices.");
        }
        
    } else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

void displayDevices() {
    auto devices = sonos.getDiscoveredDevices();
    
    Serial.println("\n=== Discovered Sonos Devices ===");
    if (devices.size() == 0) {
        Serial.println("No devices found.");
    } else {
        for (int i = 0; i < devices.size(); i++) {
            Serial.print(String(i + 1) + ". ");
            Serial.print(devices[i].name);
            Serial.print(" (" + devices[i].ip + ")");
            if (devices[i].ip == currentDeviceIP) {
                Serial.print(" [SELECTED]");
            }
            Serial.println();
        }
        Serial.println("\nTotal devices: " + String(devices.size()));
    }
    Serial.println("================================");
}

void showHelp() {
    Serial.println("\n=== Available Commands ===");
    Serial.println("play      - Start playback");
    Serial.println("pause     - Pause playback");
    Serial.println("stop      - Stop playback");
    Serial.println("next      - Skip to next track");
    Serial.println("prev      - Go to previous track");
    Serial.println("vol+      - Increase volume by 5");
    Serial.println("vol-      - Decrease volume by 5");
    Serial.println("mute      - Mute speaker");
    Serial.println("unmute    - Unmute speaker");
    Serial.println("list      - List all devices");
    Serial.println("select #  - Select device by number");
    Serial.println("help      - Show this help");
    Serial.println("==========================");
}
