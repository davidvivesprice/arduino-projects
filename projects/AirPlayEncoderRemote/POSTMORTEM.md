# AirPlay/HomeKit Encoder Remote - POSTMORTEM

## TL;DR: This approach doesn't work and was fundamentally flawed.

**Date:** November 1, 2025
**Status:** ABANDONED
**Reason:** HomeKit Television accessories cannot control non-HomeKit devices

---

## What We Tried To Build

A rotary encoder ESP32 device that would:
- Use HomeKit protocol to control AirPlay devices
- Specifically control an Anthem MRX receiver's volume
- Work via physical encoder (rotate for volume, click for play/pause)
- Show up in iOS Home app as a TV remote

## Why It Doesn't Work

### Fundamental Architecture Problem

**HomeKit accessories are ENDPOINTS, not CONTROLLERS.**

HomeKit works like this:
```
iPhone (Controller)
    ↓
    → Controls HomeKit Accessory (Light, Speaker, TV, etc.)
```

What we built:
```
ESP32 HomeKit Accessory (pretending to be a TV)
    ↓
    → ??? How does this control OTHER devices? IT DOESN'T.
```

### The Core Misunderstanding

HomeKit accessories **receive commands**, they don't **send commands** to other devices.

When you create a `Service::Television` in HomeSpan, you're telling iOS:
- "I am a TV that you can control"
- NOT "I am a remote that controls other TVs"

The volume characteristic we created? **It's the TV's own volume**, not a control for external devices.

### Specific Issues

1. **TelevisionSpeaker volume control** - Only controls the accessory's internal state, doesn't send commands anywhere
2. **RemoteKey presses** - Recorded but have no destination to send to
3. **Anthem MRX is not HomeKit-enabled** - It only receives AirPlay audio streams, not HomeKit control commands
4. **No way to "target" another device** - HomeKit protocol has no concept of one accessory controlling another

### The Pairing Hell

Getting this to pair was a nightmare:
- OSStatus -6700 errors due to missing service linking
- Television accessories require complex service relationships
- InputSource and TelevisionSpeaker must be linked to TV service
- Missing Identifier characteristics cause silent failures
- SALT generation bugs (1 in 256 chance of bad pairing code)
- 2.4GHz WiFi requirements
- Massive code size (1.5MB+ for HomeSpan library)

All this complexity just to create a **non-functional** accessory.

## What Actually Works (But We Didn't Build)

### Option 1: Direct IP Control (What we should have done from the start)

```
ESP32 Encoder
    ↓
    → HTTP/Telnet to Anthem at 192.168.x.x:14999
    → Send "MVLM50" (set main volume to 50)
    ✓ Works regardless of AirPlay
    ✓ Full receiver control
    ✓ Simple, direct, no HomeKit complexity
```

**Anthem MRX Network Protocol:**
- Port: 14999 (telnet)
- Commands: Simple text strings
- Example: `MVLM75` = set main volume to 75
- Response: Acknowledgment

This would have taken **10% of the code** and actually worked.

### Option 2: HomeKit Bridge (Not what we need here)

You can create a HomeKit **bridge** that:
- Translates HomeKit commands to Anthem commands
- Runs on always-on device (Raspberry Pi, Mac, etc.)
- Appears as a TV in Home app
- ESP32 not powerful enough for this

Projects like Homebridge do this, but it's overkill for a simple remote.

### Option 3: IR Blaster (Old school but reliable)

```
ESP32 Encoder
    ↓
    → IR LED sends Anthem remote codes
    ✓ Works like original remote
    ✓ No network required
    ✓ Simple, proven tech
```

## Lessons Learned

### What Went Wrong

1. **Didn't validate the architecture before coding** - Should have asked "Can HomeKit accessories control other devices?" (Answer: NO)

2. **Assumed HomeKit was the right protocol** - Just because Anthem supports AirPlay doesn't mean it supports HomeKit control

3. **Overcomplicated the solution** - Spent hours on HomeSpan when a simple HTTP client would work

4. **Followed the Sonos pattern blindly** - Sonos has an HTTP API we can call. Anthem has an HTTP API we can call. We should have called it directly, not tried to shoehorn HomeKit in.

5. **The HomeKit pairing process masked the real problem** - Spent so much time fixing pairing errors that we didn't stop to ask if the approach made sense

### Red Flags We Ignored

- "Control AirPlay devices" - AirPlay is for audio streaming, not control
- HomeSpan examples are all accessories (lights, switches), not controllers
- No examples of "HomeKit remote that controls other devices"
- 1.5MB library size for something that should be simple

### What We Should Have Asked

- "Does Anthem MRX expose a control API?" (YES - port 14999)
- "Is HomeKit the right tool for this?" (NO)
- "Can I find ANY example of a HomeKit accessory controlling another device?" (NO, because it's impossible)

## The Correct Approach (For Future Reference)

**Simple Direct Control:**

```cpp
// Connect to Anthem
WiFiClient client;
client.connect("192.168.x.x", 14999);

// Rotate encoder up
client.println("MVLM+5");  // Increase volume by 5

// Rotate encoder down
client.println("MVLM-5");  // Decrease volume by 5

// Button press
client.println("PMAIN");  // Power toggle
```

**That's it.** No HomeKit. No 1.5MB library. No pairing hell. Just works.

## Salvageable Parts

From this project, we can reuse:
- ✅ Encoder reading logic (works great)
- ✅ Button debouncing and multi-click detection
- ✅ WiFi connection handling
- ✅ Hardware setup (GPIO pins, pull-ups)

What to throw away:
- ❌ Entire HomeSpan integration
- ❌ Television/Speaker/InputSource services
- ❌ HomeKit pairing complexity
- ❌ The entire concept

## Cost Analysis

**Time spent on doomed approach:**
- HomeSpan research: 30 min
- Initial implementation: 45 min
- Fixing compilation errors: 20 min
- Fixing pairing errors (OSStatus -6700): 60 min
- Service linking fixes: 20 min
- Custom setup code: 15 min
- **Total: ~3 hours**

**Time correct approach would take:**
- Anthem protocol research: 10 min
- Simple TCP client code: 20 min
- **Total: ~30 minutes**

**We wasted 2.5 hours on a fundamentally broken approach.**

## Why This Felt Like It Should Work

**It's a reasonable assumption that:**
- Anthem supports AirPlay → It should support Apple control protocols
- HomeKit has TV accessories → They should control TVs
- HomeSpan can create remotes → They should control things

**But the reality:**
- AirPlay = streaming protocol (one-way audio)
- HomeKit TV accessories = controllable endpoints, not controllers
- HomeKit has no peer-to-peer device control

## Conclusion

**HomeKit is the wrong tool for this job.**

The correct solution is direct IP control using Anthem's network protocol. Simple, reliable, and actually works.

**Next step:** Abandon this HomeKit approach entirely and build a direct Anthem IP controller instead.

---

## Appendix: What Actually Happens When You Use It

Current behavior:
1. Rotate encoder → Volume number changes in ESP32 memory
2. Home app shows the volume change
3. **Nothing happens to the Anthem receiver**
4. The volume value is meaningless - it controls nothing

It's like having a steering wheel that's not connected to any wheels. Sure, it rotates. But the car doesn't turn.

## See Also

- Sonos version: Actually works because we call Sonos HTTP API directly
- What we should build next: `AnthemEncoderRemote` with direct IP control
- HomeSpan docs: Great for building accessories, useless for controlling other devices
