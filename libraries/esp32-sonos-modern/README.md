# Sonos Library

Arduino library for ESP32 to control Sonos speakers over WiFi.

## Features

- Automatic device discovery
- Playback control (play, pause, stop, next, previous)
- Volume control and mute
- Multiple device management

## API Reference

### Initialization
- `begin()` - Initialize the library
- `end()` - Clean shutdown
- `isInitialized()` - Check initialization status

### Device Discovery
- `discoverDevices()` - Find Sonos speakers on network
- `getDiscoveredDevices()` - Get list of found devices
- `getDeviceByName(name)` - Find device by room name
- `getDeviceByIP(ip)` - Find device by IP address
- `getDeviceCount()` - Get number of discovered devices

### Playback Control
- `play(deviceIP)` - Start playback
- `pause(deviceIP)` - Pause playback
- `stop(deviceIP)` - Stop playback
- `next(deviceIP)` - Skip to next track
- `previous(deviceIP)` - Go to previous track

### Volume Control
- `setVolume(deviceIP, volume)` - Set volume (0-100)
- `getVolume(deviceIP, &volume)` - Get current volume
- `increaseVolume(deviceIP, increment)` - Increase volume
- `decreaseVolume(deviceIP, decrement)` - Decrease volume
- `setMute(deviceIP, mute)` - Set mute state (true/false)

### Configuration
- `setConfig(config)` - Set library configuration
- `getConfig()` - Get current configuration

### Callbacks
- `setDeviceFoundCallback(callback)` - Called when device discovered
- `setLogCallback(callback)` - Custom logging handler

### Utilities
- `getErrorString(result)` - Convert error code to string

### Error Codes
- `SUCCESS` - Operation completed successfully
- `ERROR_NETWORK` - Network communication error
- `ERROR_TIMEOUT` - Operation timed out
- `ERROR_INVALID_DEVICE` - Invalid or offline device
- `ERROR_SOAP_FAULT` - SOAP protocol error
- `ERROR_NO_MEMORY` - Insufficient memory
- `ERROR_INVALID_PARAM` - Invalid parameter

## Contributions

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.
