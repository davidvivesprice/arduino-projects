#ifndef SONOS_H
#define SONOS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <vector>
#include <functional>

// Error codes for Sonos operations
enum class SonosResult {
    SUCCESS = 0,
    ERROR_NETWORK = -1,
    ERROR_TIMEOUT = -2,
    ERROR_INVALID_DEVICE = -3,
    ERROR_SOAP_FAULT = -4,
    ERROR_NO_MEMORY = -5,
    ERROR_INVALID_PARAM = -6
};

// Structure to hold discovered Sonos device information
struct SonosDevice {
    String name;
    String ip;
};

// Configuration for the Sonos library
struct SonosConfig {
    uint16_t discoveryTimeoutMs = 5000;
    uint16_t soapTimeoutMs = 10000;
    uint8_t maxRetries = 3;
    uint16_t discoveryPort = 1901;
    bool enableLogging = false;
};

class Sonos {
private:
    // Private members
    WiFiUDP _udp;
    HTTPClient _http;
    std::vector<SonosDevice> _devices;
    SonosConfig _config;
    bool _initialized = false;
    
    // Network constants
    static const char* SSDP_MULTICAST_IP;
    static const int SSDP_PORT = 1900;
    static const char* SONOS_DEVICE_TYPE;
    static const char* SSDP_SEARCH_REQUEST;
    
    // SOAP templates
    static const char* SOAP_ENVELOPE_TEMPLATE;
    static const char* VOLUME_SET_TEMPLATE;
    static const char* VOLUME_GET_TEMPLATE;
    static const char* MUTE_SET_TEMPLATE;
    static const char* TRANSPORT_PLAY_TEMPLATE;
    static const char* TRANSPORT_PAUSE_TEMPLATE;
    static const char* TRANSPORT_STOP_TEMPLATE;
    static const char* TRANSPORT_NEXT_TEMPLATE;
    static const char* TRANSPORT_PREVIOUS_TEMPLATE;
    
    // Private methods
    bool parseDeviceDescription(const String& xml, SonosDevice& device);
    String extractXmlValue(const String& xml, const String& tag);
    SonosResult sendSoapRequest(const String& deviceIP, const String& service, 
                               const String& action, const String& body, String& response);
    String formatSoapRequest(const String& service, const String& action, const String& body);
    bool isValidIP(const String& ip);
    void logMessage(const String& message);
    
public:
    // Constructor
    Sonos();
    Sonos(const SonosConfig& config);
    
    // Initialization
    SonosResult begin();
    void end();
    bool isInitialized() const { return _initialized; }
    
    // Device discovery
    SonosResult discoverDevices();
    std::vector<SonosDevice> getDiscoveredDevices() const;
    SonosDevice* getDeviceByName(const String& name);
    SonosDevice* getDeviceByIP(const String& ip);
    int getDeviceCount() const { return _devices.size(); }
    
    // Volume control
    SonosResult setVolume(const String& deviceIP, int volume);
    SonosResult getVolume(const String& deviceIP, int& volume);
    SonosResult increaseVolume(const String& deviceIP, int increment = 5);
    SonosResult decreaseVolume(const String& deviceIP, int decrement = 5);
    SonosResult setMute(const String& deviceIP, bool mute);
    
    // Playback control
    SonosResult play(const String& deviceIP);
    SonosResult pause(const String& deviceIP);
    SonosResult stop(const String& deviceIP);
    SonosResult next(const String& deviceIP);
    SonosResult previous(const String& deviceIP);
    
    // Utility methods
    void setConfig(const SonosConfig& config) { _config = config; }
    SonosConfig getConfig() const { return _config; }
    String getErrorString(SonosResult result);
    
    // Event callbacks
    typedef std::function<void(const SonosDevice&)> DeviceFoundCallback;
    typedef std::function<void(const String&)> LogCallback;
    
    void setDeviceFoundCallback(DeviceFoundCallback callback) { _deviceFoundCallback = callback; }
    void setLogCallback(LogCallback callback) { _logCallback = callback; }
    
private:
    DeviceFoundCallback _deviceFoundCallback = nullptr;
    LogCallback _logCallback = nullptr;
};

#endif // SONOS_H
