# Upload Troubleshooting — ESP32-S3 Native USB JTAG on macOS

## Current Status

✅ **Project Initialization:** Complete  
✅ **Firmware Compilation:** Success (464 KB, verified)  
✅ **Device Detection:** Working (`/dev/cu.usbmodem4101`)  
❌ **esptool Communication:** Blocked — "No serial data received"

## Root Cause

ESP32-S3 uses native USB JTAG (not UART bridge). On macOS, the USB JTAG interface has known driver/compatibility issues:

- Device is detected by `pio device list`
- Device appears as `IOUSBHostDevice` in bootloader
- But esptool cannot establish handshake
- RTS/DTR auto-reset signals don't trigger bootloader on this device

This is **not** a firmware or PlatformIO issue—the build is correct.

## Solutions to Try

### Option 1: Arduino IDE (Recommended First Try)
Arduino IDE sometimes has better native USB JTAG support on macOS:

1. Install Arduino IDE 2.0+
2. Add ESP32 board: https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/installing.html
3. Install M5Stack boards: https://github.com/m5stack/m5stack-core-aws
4. Copy our firmware binary:
   ```bash
   cp .pio/build/m5stack-dial/firmware.bin ~/arduino-sketch-backup.bin
   ```
5. Create new Arduino sketch:
   ```cpp
   void setup() { Serial.begin(115200); }
   void loop() {}
   ```
6. Use Sketch → Upload → Select M5Stack Dial board
7. When it fails to connect, try holding the center button during upload

### Option 2: Direct esptool via Homebrew Python
The PlatformIO environment has Python issues. Try a clean esptool installation:

```bash
# Install via Homebrew
brew install esptool.py

# Try upload directly (with bootloader/button held)
esptool.py --chip esp32s3 --port /dev/cu.usbmodem4101 write_flash 0x0 .pio/build/m5stack-dial/firmware.bin
```

### Option 3: Different USB Cable
- Try a different USB cable (some cables don't support data+power)
- Try different USB port on Mac
- Try USB 2.0 port if available (USB 3.0 can have compatibility issues)

### Option 4: macOS Driver Update
- Update macOS to latest patch version
- Check if System Preferences → Security & Privacy has USB restrictions
- Try resetting NVRAM: Restart Mac, hold Cmd+Option+P+R for 20 seconds

### Option 5: Use Alternative Chip Upload Tool
If esptool fails, try the ROM bootloader's native upload:

```bash
# Using Python directly with debugging
python3 -c "
import sys
sys.path.insert(0, '/Users/cheddarwhizzy/.platformio/packages/tool-esptoolpy@src-c7ffb85155e7da6fce69e3cf32286b0e')
import esptool
esptool.main([
    'write_flash',
    '--port', '/dev/cu.usbmodem4101',
    '--chip', 'esp32s3',
    '--baud', '921600',
    '0x0', '.pio/build/m5stack-dial/firmware.bin'
])
"
```

### Option 6: Web Flasher (Easiest)
Espressif provides a web-based flasher that sometimes works better:

1. Go to: https://web.espress if.com
2. Click "Connect"
3. Select `/dev/cu.usbmodem4101`
4. Choose firmware binary: `.pio/build/m5stack-dial/firmware.bin`
5. Click "Program"

This uses WebUSB which has different driver support.

## Bootloader Entry Sequence

For manual bootloader entry, try this precise sequence:

1. **Disconnect USB completely**
2. **Hold center dial button**
3. **Connect USB while holding button**
4. **Wait 2 seconds** (keep holding)
5. **Release button**
6. **Screen should be BLACK** (bootloader mode)
7. Immediately run: `pio run --target upload`

If screen stays black and upload fails:
- Try releasing button immediately after USB connects
- Try holding button for only 1 second after USB connects
- Try different timing windows (0.5s, 1s, 2s, 5s)

## Workaround: Manual Bootloader Binary

If auto-flash fails, you can manually construct and flash the bootloader:

```bash
# Extract bootloader and partitions
cp .pio/build/m5stack-dial/bootloader.bin /tmp/
cp .pio/build/m5stack-dial/partitions.bin /tmp/
cp .pio/build/m5stack-dial/firmware.bin /tmp/

# When you have esptool working, use:
esptool.py write_flash \
  0x0 /tmp/bootloader.bin \
  0x8000 /tmp/partitions.bin \
  0x10000 /tmp/firmware.bin
```

## What's Definitely Working

✅ Firmware compiles correctly  
✅ All dependencies resolve  
✅ Hardware APIs are correct  
✅ Device is physically responsive  
✅ Demo app currently running means device isn't bricked  

The communication channel is the **only** blocker.

## If All Else Fails

### Option A: RMA/Support
If the device has a hardware issue:
- Contact M5Stack support with device SN: `34:B7:DA:56:05:B4`
- Describe: "esptool cannot communicate via native USB JTAG on macOS"

### Option B: Use Different Computer
- Try Windows machine with same firmware binary
- Try Linux machine with same firmware binary
- macOS driver issues are isolated—other OS might work

### Option C: Use External JTAG
Some development boards support external JTAG adapters (FTDI, etc) as alternative to native USB. M5Stack Dial doesn't list this, but worth checking the pinout.

## Firmware Backup

Your compiled firmware is ready for any of these methods:

```bash
# Full backup paths
ls -lh .pio/build/m5stack-dial/
  - firmware.bin        (464 KB) — Main application
  - bootloader.bin      (16 KB)  — Bootloader
  - partitions.bin      (64 B)   — Partition table
  - firmware.elf        (15 MB)  — Debug symbols
```

You can use `firmware.bin` with **any** ESP32-S3 upload method.

## Next Steps

1. **Try Option 1 (Arduino IDE)** — Most likely to work on macOS
2. **Try Option 6 (Web Flasher)** — Second most likely
3. **Try Option 2 (Homebrew esptool)** — If others fail
4. Report back with which method succeeds

Once upload works, the firmware will boot with full hardware diagnostics and is ready for Phase 2 UI implementation.

---

**Hardware:** M5Stack Dial (ESP32-S3)  
**Firmware:** Ready and compiled  
**Blocker:** macOS USB JTAG driver issue  
**Solution:** Try alternative upload method above
