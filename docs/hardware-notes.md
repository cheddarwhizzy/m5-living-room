# M5 Dial Hardware Notes

## Verified Configuration

### Microcontroller
- **Device:** M5Stack Dial
- **Chip:** Espressif ESP32-S3-WROOM-1-N16R8
- **Flash:** 16 MB
- **PSRAM:** 8 MB (SPIRAM)
- **Clock:** 240 MHz (dual core)

### PlatformIO Configuration
- **Platform:** `platformio/espressif32` v7.0.1
- **Board ID:** `m5stack-cores3` (ESP32-S3, closest match; M5 Dial board definition not yet in PlatformIO stable)
- **Framework:** Arduino
- **C++ Standard:** C++17

### Official Libraries
- **M5Dial:** 1.0.3 (PlatformIO Registry)
- **M5Unified:** 0.2.19 (PlatformIO Registry)
- **M5GFX:** 0.2.26 (PlatformIO Registry)

All three are required:
- `M5GFX`: Low-level graphics and display driver
- `M5Unified`: Cross-device hardware abstraction
- `M5Dial`: M5 Dial-specific features (encoder, button, touch on round display)

## Display

### Specifications
- **Resolution:** 240 × 240 pixels (square framebuffer, round display lens)
- **Type:** IPS LCD
- **Interface:** SPI
- **Color Depth:** 16-bit (RGB565)

### Initialization
```cpp
M5Dial.begin(cfg, true, false);
M5Dial.Display.fillScreen(TFT_BLACK);
M5Dial.Display.setCursor(x, y);
M5Dial.Display.printf("text");
```

The display is accessible via `M5Dial.Display` (instance of `M5GFX::LGFX_M5Dial`).

### Color Names (TFT_*)
TFT_BLACK, TFT_DARKGREY, TFT_GREY, TFT_WHITE, TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_BLUE, TFT_PURPLE, TFT_MAGENTA

### Update Calls
Always call `M5Dial.update()` in the main loop before reading input devices.

## Rotary Encoder

### API
```cpp
M5Dial.Encoder.count()  // Returns incremental count (positive = clockwise)
M5Dial.Encoder.setCount(0)  // Reset counter
```

### Behavior
- Increments/decrements as the dial is turned
- One "step" per physical detent (~20 per full rotation)
- Count accumulates until read and reset
- Non-blocking

### Notes
- The M5Dial library maps encoder input to a counter
- No debounce configuration needed (handled in library)
- Encoder position is relative to the last read, not absolute

## Center Button

### API
```cpp
M5Dial.BtnA.wasPressed()   // True if pressed since last update
M5Dial.BtnA.isPressed()     // True if currently held
M5Dial.BtnA.wasReleased()   // True if released since last update
M5Dial.BtnA.waitRelease()   // Blocks until button released
```

### Behavior
- Single tactile button at center of dial
- Press to select/confirm in UI
- Debouncing handled in library

### Notes
- `wasPressed()` is edge-triggered; call only once per cycle
- `waitRelease()` blocks execution (use for critical confirmations only)
- In this phase, intentionally non-blocking to keep main loop responsive

## Capacitive Touchscreen

### API
```cpp
M5Dial.Touch.getDetail()  // Returns touch_detail_t struct

struct touch_detail_t {
  m5::Touch::State state;  // Touched, Released, Moving, None
  int x, y;                // Coordinates (0-239 for both)
  uint16_t size;           // Touch area size
};
```

### Behavior
- Overlay capacitive touch, 240×240 resolution
- Reports one touch point (single-touch)
- Coordinates map to display pixel coordinates

### States
- `State::Touched` – Finger just touched screen
- `State::Moving` – Finger moved while touching
- `State::Released` – Finger just lifted
- `State::None` – No touch

### Notes
- Only call `getDetail()` once per loop (it returns the current state)
- Touch is not affected by display rotation
- Round display: corners may not register touches (hardware lens limitation)

## USB/Serial

### Baud Rate
- **115200 bps** (default, no configuration needed)
- Supports auto-reset on upload via DTR/RTS handshake

### Upload Configuration
```ini
upload_speed = 460800      # Fast upload
monitor_speed = 115200     # Serial monitor speed
monitor_rts = 0            # No RTS
monitor_dtr = 0            # No DTR
```

### Boot Sequence
1. Connect USB (auto-reset is handled)
2. Device enters bootloader automatically
3. Firmware uploads via UART
4. Device resets and runs firmware
5. Serial output available immediately

### No Manual Steps Required
- No boot button press needed
- No manual reset required
- CH340G driver handles everything

## Memory Layout

### Flash (16 MB)
- Firmware binary: ~800 KB (typical)
- SPIFFS or LittleFS can be added later for settings storage

### PSRAM (8 MB)
- Available for frame buffers, large allocations
- Used by M5GFX for display rendering

### Heap
- Typical free heap on boot: 300+ KB
- Sufficient for simple UI applications without dynamic allocation in loops

## Pin Mapping (FYI, abstracted by M5Dial library)

The M5Dial library handles all GPIO initialization. If you need raw pin access:
- **Display SPI:** Managed by M5GFX (GPIO 1, 2, 42, 41)
- **Encoder:** GPIO 40 (button A)
- **Button:** GPIO 40 (same as encoder click)
- **Touch:** I2C-based (GPIO 12, 11)

Do not directly configure these pins; use the M5Dial library APIs instead.

## Known Behavior

### Display Updates
- Full screen redraw is fast (~40 ms) but can cause flicker
- Drawing to specific regions is preferred for smooth updates
- The round display has rounded corners; content in the four corners will be clipped

### Encoder Edge Cases
- Turning very fast may skip steps (normal behavior for mechanical encoders)
- No acceleration or velocity tracking in the library
- Acceleration can be implemented in application code

### Power Consumption
- Full brightness display: ~200 mA
- Idle with display dimmed: ~50 mA
- Sleep mode: ~10 mA (not explored in Phase 1)

## References Consulted

- [M5Dial GitHub Repository](https://github.com/m5stack/M5Dial)
- [M5Dial Examples](https://github.com/m5stack/M5Dial/tree/main/examples)
- [M5Unified Documentation](https://github.com/m5stack/M5Unified)
- [M5GFX GitHub](https://github.com/m5stack/M5GFX)
- [PlatformIO ESP32 Board Definitions](https://github.com/platformio/platform-espressif32)
- [Espressif ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)

---

**Updated:** July 23, 2026  
**Scope:** Phase 1 Hardware Initialization
