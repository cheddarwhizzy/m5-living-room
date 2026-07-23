# M5 Dial Lighting Controller

## Project Purpose

A smart lighting control interface for a single room using an M5Stack Dial with Home Assistant integration. The device provides rotary encoder brightness control and touchscreen scene selection.

This is **Phase 1: Hardware Initialization** — proof that the M5 Dial hardware works and all sensors respond correctly.

## Current Scope

- Hardware validation only
- Display: 240×240 round LCD
- Rotary encoder: brightness adjustment (1–100%)
- Center button: press detection
- Touchscreen: touch coordinate logging
- No Home Assistant integration yet
- No persistent storage
- No Wi-Fi or networking

## Hardware Requirements

### Device
- **M5Stack Dial** (ESP32-S3, 240×240 IPS round display, rotary encoder, capacitive touch)
- Micro-USB cable for programming and power
- Optional: USB power adapter (5V/2A recommended)

### Development Environment
- macOS / Linux / Windows
- USB cable (data + power capable)
- CH340G or FTDI USB-to-Serial driver (usually auto-installed)

## Software Requirements

### Installation

**PlatformIO CLI:**
```bash
# Install Python (if not present)
brew install python3

# Install PlatformIO
pip3 install platformio

# Verify
pio --version
```

**Visual Studio Code + PlatformIO Extension (optional but recommended):**
1. Install VS Code
2. Install the "PlatformIO IDE" extension from the Extensions Marketplace
3. Reload VS Code

## Building

```bash
# Navigate to project directory
cd /Users/cheddarwhizzy/cheddar/cheddarwhizzy/m5-living-room

# Build for M5Stack Dial
pio run

# Build and upload (device must be connected)
pio run --target upload

# Open serial monitor (after upload, or during debugging)
pio device monitor

# Combined: build, upload, and monitor
pio run --target upload && pio device monitor
```

## Serial Monitor Output

Expected boot output:

```
=== M5 Dial Lighting Controller ===
Firmware: M5 Dial Lighting Controller
Version: 0.1.0
Build Date: Jul 23 2026
Chip Model: esp32s3
Chip Revision: 0
Flash Size: 16 MB
PSRAM: 8 MB
Free Heap: 300 KB

M5 Dial hardware initialized
  - Display: 240x240 round LCD
  - Encoder: Rotary input for brightness
  - Button: Center dial button
  - Touch: Capacitive touchscreen

Hardware initialized. Starting main loop.
```

When you interact with the device, you'll see:

```
[Encoder] Brightness: 45%
[Encoder] Brightness: 46%
[Button] Pressed at 12345 ms
[Touch] x=120, y=120 at 12346 ms
```

## Screen Behavior

### Normal State
- Title: "M5 DIAL" (top)
- Label: "Brightness" (center-upper)
- Large percentage: "50%" (center, white, large font)
- Arc indicator: orange arc representing current brightness (bottom)

### Encoder Interaction
- Rotate clockwise: brightness increases (1% per tick)
- Rotate counterclockwise: brightness decreases (1% per tick)
- Range: 1% to 100%
- Display updates immediately as you turn the dial

### Button Interaction
- Press center dial button
- Screen shows "Pressed" (yellow text, center)
- Returns to brightness display after ~500ms
- Event logged to serial

### Touchscreen Interaction
- Touch anywhere on the screen
- Screen shows "Touch" (yellow text, center)
- Coordinates printed to serial
- Returns to brightness display after ~500ms

## Hardware Testing Checklist

Before proceeding to Phase 2 (UI refinement), verify:

- [ ] **Boot Messages:** Serial monitor shows all boot messages clearly
- [ ] **Display:** Screen shows "M5 DIAL" title and "Brightness 50%" clearly
- [ ] **Encoder Clockwise:** Rotating encoder clockwise increases brightness (45%, 46%, 47%...)
- [ ] **Encoder Counterclockwise:** Rotating counterclockwise decreases brightness
- [ ] **Encoder Limits:** Brightness clamps at 1% and 100% (doesn't wrap)
- [ ] **Button Press:** Pressing center button shows "Pressed" briefly, then returns to brightness
- [ ] **Touch Detection:** Touching screen shows "Touch", and coordinates appear in serial
- [ ] **No Crashes:** Device remains responsive after multiple interactions
- [ ] **Serial Logging:** All interactions logged cleanly to serial monitor

## Current Limitations

- No Home Assistant connection (Phase 2)
- No scene selection (Phase 2)
- No persistent settings (Phase 3)
- No Wi-Fi provisioning (Phase 3)
- No MQTT (Phase 4)
- Display backlight always at maximum brightness
- No acceleration on encoder (1% per step, linear only)
- No haptic feedback simulation
- Touch coordinates logged but not interpreted

## Next Steps (Phase 2)

1. Implement polished brightness UI with animations
2. Add state machine for brightness vs. scene modes
3. Implement scene selection (6 predefined scenes)
4. Add visual feedback for scene selection
5. Implement dial press to apply scene
6. Refine encoder acceleration
7. Add haptic feedback simulation to serial output

## File Structure

```
.
├── platformio.ini          # Build configuration
├── .gitignore             # Git ignore rules
├── README.md              # This file
├── include/
│   ├── AppConfig.h        # Compile-time configuration
│   └── BuildInfo.h        # Version and build info
├── src/
│   └── main.cpp           # Main firmware
├── test/                  # (Reserved for unit tests)
└── docs/
    └── hardware-notes.md  # Hardware reference
```

## Troubleshooting

### Device not detected
```bash
# List connected USB devices
pio device list

# If M5Stack Dial doesn't appear, check:
# 1. USB cable (use a data-capable cable, not power-only)
# 2. Driver: Install CH340G drivers from https://sparks.gogo.co.nz/ch340.html
# 3. Device: Try different USB ports
# 4. Reset: Hold the power button for 3 seconds
```

### Upload timeout
```bash
# Reduce upload speed if timeout occurs
# Edit platformio.ini:
# upload_speed = 115200  # Slower speed
```

### Serial monitor not responding
```bash
# Close and reconnect
pio device monitor

# Or in VS Code: use the serial monitor from the PlatformIO tab
```

### Garbage characters in serial output
```bash
# Check baud rate (should be 115200)
# Edit platformio.ini if needed:
# monitor_speed = 115200
```

## References

- [M5Stack Dial Official Docs](https://docs.m5stack.com/en/core/dial)
- [M5Dial GitHub Library](https://github.com/m5stack/M5Dial)
- [M5Unified Documentation](https://github.com/m5stack/M5Unified)
- [PlatformIO Espressif32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Arduino ESP32 Reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/)

## License

MIT (placeholder — adjust as needed)

## Author

Brett Porter (cheddarwhizzy)

---

**Last Updated:** July 23, 2026  
**Firmware Version:** 0.1.0  
**Status:** Phase 1 — Hardware Initialization
