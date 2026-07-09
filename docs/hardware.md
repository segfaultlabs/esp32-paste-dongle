# Hardware Guide

## Supported Chips

This project supports two hardware targets with a unified codebase:

- **ESP32 (classic)** — Use **BLE HID keyboard** mode. No USB-OTG, but has BLE and WiFi.
- **ESP32-S3** — Use **USB HID keyboard** mode (or BLE mode). Has native USB-OTG + BLE + WiFi.

Not supported:
- **ESP32-S2** — Has USB-OTG but no BLE, so BLE mode is impossible.
- **ESP32-C3/C6** — No USB-OTG.

## Recommended Boards

### For BLE Mode (Classic ESP32): ESP32-DevKitC
- **Pros:** Cheap, widely available, the chip you likely already have.
- **Cons:** Needs the target PC to have Bluetooth; device is wireless, not a USB dongle.
- **Notes:**
  - Flash using the UART USB port.
  - Device advertises as a BLE keyboard; target PC pairs with it.

### For USB Mode: ESP32-S3-DevKitC-1
- **Pros:** Two USB ports (UART + native USB), good for development.
- **Cons:** Not a dongle form factor.
- **Notes:**
  - Use the **USB** port for HID connection to the target PC.
  - Use the **UART** port for flashing and Serial Monitor.

### For Final USB Form Factor: LilyGO T-Dongle-S3
- **Pros:** Compact USB-C dongle shape, optional built-in screen, plugs directly into a PC.
- **Cons:** Slightly more expensive; screen adds complexity if used.
- **Where to buy:** LilyGO store, AliExpress.

### Other Options
- **ESP32-S3-Zero / S3-Mini** — For USB mode; need a carrier board with USB connector.
- **ESP32-WROOM-32 dev kit** — For BLE mode.

## Minimum Wiring

If using a bare module or a DevKit, the wiring is simple:

| Function | ESP32-S3 Pin | Connect To |
|---|---|---|
| USB D+ | GPIO 20 | USB connector D+ |
| USB D- | GPIO 19 | USB connector D- |
| 3.3V | 3.3V | USB connector VBUS via 3.3V regulator |
| GND | GND | USB connector GND |

Most DevKits have the USB connector already populated, so no manual wiring is needed for USB.

## Optional Components

### Status LED
- Connect an LED + 220 Ω resistor between a GPIO pin and GND.
- Use to indicate: idle, typing, error.

### Push Button
- Connect a momentary button between a GPIO pin and GND.
- Use for: safe mode, factory reset, cancel current paste.

### OLED Screen (I2C)
- SSD1306 128x64 or similar.
- SDA → GPIO 8, SCL → GPIO 9 (or board-specific I2C pins).
- Shows: AP name, IP address, status, progress.

### SD Card (Future)
- Useful for storing very large scripts or logs.
- Not required for the core paste/snippet features.

## Power

- The ESP32-S3 is powered from the USB port of the target PC (5 V → onboard 3.3 V regulator).
- Typical current draw: ~100–250 mA when WiFi is active.
- Ensure the target USB port can supply at least 500 mA.

## Cables

- Use a **data-capable** USB cable. Many USB-C cables are power-only and will not work for USB HID.
- For DevKitC: use the native USB port for the target PC and the UART port for flashing.

## Recommended First Build

1. Buy an **ESP32-S3-DevKitC-1**.
2. Connect the native USB port to your PC.
3. Flash the proof-of-concept firmware.
4. Verify the device appears as a USB keyboard.
5. Once proven, migrate to a dongle form factor (LilyGO T-Dongle-S3) if desired.
