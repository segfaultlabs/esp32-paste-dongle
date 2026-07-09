# Research: Existing Phone-to-PC Paste Dongles

## Problem We Are Solving
Move large blocks of text from a phone to a computer that only accepts a standard USB keyboard. The computer must not require any special software, network access, cloud service, or e-mail. Use cases include air-gapped machines, BIOS/pre-boot environments, locked-down kiosks, and untrusted PCs where you do not want to log into a password manager or cloud account.

## Core Technical Pattern
Every viable solution found follows the same pattern:
1. A microcontroller with **USB-OTG** capability enumerates as a USB HID keyboard on the target PC.
2. The phone sends text to the microcontroller over **BLE** or **WiFi**.
3. The microcontroller converts the text into HID key reports and "types" it into the PC.

For this project, the **ESP32-S3** is the dominant choice because it has a native USB-OTG peripheral and WiFi/BLE on the same chip. Earlier ESP32 variants lack USB-OTG and cannot act as a USB device without external hardware.

## Projects Reviewed

### 1. BluKeyborg / blue_keyboard
- **Author:** larrylart
- **License:** Not explicitly verified; assume review before reuse.
- **Hardware:** ESP32-S3 USB dongles/boards; primary reference is LilyGO S3 T-Dongle.
- **Connectivity:** Bluetooth Low Energy.
- **Clients:** Native Android app, iOS app (pending), Linux CLI.
- **Security:** BLE bonding + application-level mTLS.
- **Features:** Secure credential/text input, keyboard layout management, macro support on roadmap.
- **What we can learn/reuse:**
  - Keyboard layout/localization architecture.
  - BLE-to-HID state machine (if we later add BLE fallback).
  - LilyGO T-Dongle hardware notes.
- **Why not use as-is:** Requires native mobile apps; user wants WiFi AP + web UI.

### 2. ToothPaste Dongle
- **Author:** Brisk4t (pseudonymous)
- **Hardware:** ESP32-S3.
- **Connectivity:** BLE, controlled via WebBLE browser app.
- **Security:** ECDSA-encrypted keystrokes in ProtoBuf packets; Argon2 key derivation; local data encrypted.
- **Features:** Paste passwords/text into untrusted machines; keyboard + mouse HID.
- **What we can learn/reuse:**
  - WebBLE control model (no app install).
  - Security/threat model for untrusted host scenarios.
- **Why not use as-is:** WebBLE has limited browser support on iOS; user wants WiFi AP.

### 3. Gooshy
- **Type:** Commercial product.
- **Connectivity:** BLE dongle + phone app; also web target and desktop target software.
- **Features:** Encrypted on-device storage, password-manager integration, physical BLE dongle for USB-only targets.
- **What we can learn/reuse:**
  - Product-level UX for phone-to-dongle text injection.
  - Validation that there is market demand for exactly this use case.
- **Why not use as-is:** Closed source; different connectivity model.

### 4. konkop / ESP32-S3-USB-HID-Macro-Keyboard-Web-Typing-Interface
- **Author:** konkop
- **License:** MIT
- **Hardware:** ESP32-S3 DevKit (N16R8) with optional 9x Cherry MX macro keys.
- **Connectivity:** WiFi (joins existing network, mDNS `.local` address).
- **Features:**
  - USB HID keyboard via TinyUSB.
  - Web interface hosted on the ESP32.
  - Human-like typing (random delays, pauses, backspaces).
  - Physical macro keys (can be omitted for remote-only typing).
  - PlatformIO project.
- **What we can learn/reuse:**
  - Entire PlatformIO project structure.
  - TinyUSB/USB HID setup.
  - Web-UI serving approach.
  - Human-like typing implementation.
  - mDNS advertising.
- **Gaps vs. our goal:** It joins an existing WiFi network rather than creating an AP; it does not stream large text from the phone; it lacks snippet storage.

### 5. EvilDuck (S3)
- **Author:** cifertech
- **License:** MIT
- **Hardware:** ESP32-S3 with optional SD card, RGB LED, USB Type-A.
- **Connectivity:** WiFi AP + browser-based control panel.
- **Features:**
  - USB HID keyboard.
  - Full DuckScript interpreter.
  - Script upload/edit/delete/run over WiFi.
  - Live execution status and logs.
  - Password-protected AP, hidden SSID option.
  - Storage modes: internal SPIFFS, SD card, USB mass storage.
- **What we can learn/reuse:**
  - DuckScript parser/interpreter for macros.
  - Browser-based control panel patterns.
  - WiFi AP setup and password protection.
  - File upload/run/stop workflow.
- **Gaps vs. our goal:** Focused on penetration-testing workflows; UI is script-centric rather than paste-centric; no phone-optimized UI or streaming.

### 6. WUD-Ducky
- **Author:** tobozo / AprilBrother
- **Hardware:** ESP32-S2 with SD card.
- **Features:** USB HID + USB mass storage, WiFi AP, web endpoints for `/key`, `/runpayload`, file upload.
- **What we can learn/reuse:**
  - HTTP endpoints for sending keystrokes and running scripts.
  - SD card + USB mass storage dual-mode tricks.
- **Why less relevant:** ESP32-S2 has no BLE; project is older and focused on ducky payloads.

### 7. USB Nugget (Hak Cat)
- **Hardware:** ESP32-S2.
- **Features:** DuckyScript, screen, buttons, WiFi, CircuitPython.
- **What we can learn/reuse:**
  - DuckyScript examples.
  - Educational reference for HID payloads.
- **Why less relevant:** ESP32-S2, CircuitPython, security-tool form factor.

### 8. ESP32-BLE-Keyboard Library
- **Author:** T-vK, chegewara, LemmingDev
- **License:** Open source
- **What it is:** Arduino library that turns an ESP32 into a BLE HID keyboard.
- **What we can learn/reuse:**
  - HID key report construction.
  - Modifier handling.
  - BLE HID descriptor patterns.
- **Why not use directly:** User wants WiFi + web UI for this build, but useful if we add BLE fallback later.

### 9. Arduino-ESP32 USBHIDKeyboard
- **Source:** Part of the official Arduino-ESP32 core.
- **What it is:** Wrapper around TinyUSB that lets an ESP32-S3 act as a USB HID keyboard.
- **What we can learn/reuse:**
  - Simple `Keyboard.print()` / `Keyboard.press()` / `Keyboard.release()` API.
  - Reference for USB HID keyboard descriptors.

## Comparison Matrix

| Project | Chip | USB HID | WiFi AP | BLE | Web UI | Native App | Scripting | License | Reuse Potential |
|---|---|---|---|---|---|---|---|---|---|
| blue_keyboard | ESP32-S3 | Yes | No | Yes | No | Yes | Planned | ? | Medium |
| ToothPaste | ESP32-S3 | Yes | No | WebBLE | WebBLE | No | No | ? | Medium |
| Gooshy | ESP32-S3 | Yes | No | Yes | Yes | Yes | Buttons | Commercial | Low |
| konkop WiFi KB | ESP32-S3 | Yes | Joins network | No | Yes | No | Macros | MIT | **High** |
| EvilDuck S3 | ESP32-S3 | Yes | Yes | No | Yes | No | DuckScript | MIT | **High** |
| WUD-Ducky | ESP32-S2 | Yes | Yes | No | Minimal | No | Ducky | Open | Medium |
| USB Nugget | ESP32-S2 | Yes | Yes | No | No | No | Ducky | Open | Low |

## Key Takeaways
1. **ESP32-S3 is the right chip.** Native USB-OTG + WiFi + BLE in one package.
2. **konkop's project is the best starting point** for the firmware skeleton because it already does ESP32-S3 USB HID + WiFi + web UI + human-like typing and is MIT-licensed.
3. **EvilDuck-S3 is the best reference** for DuckScript/macro support and WiFi AP configuration.
4. **blue_keyboard** is the best reference for keyboard layouts and BLE fallback architecture.
5. **Streaming from the phone** is our differentiator for handling very large text without exhausting ESP32 RAM.

## Recommended Reuse Plan
| Component | Primary Source | Notes |
|---|---|---|
| Project skeleton / `platformio.ini` | konkop | MIT license; adapt board selection. |
| USB HID setup | konkop + Arduino `USBHIDKeyboard` | Use TinyUSB directly if we need lower-level control. |
| Web UI serving | konkop | Adapt to AP mode instead of station mode. |
| Human-like typing | konkop | Copy delay/pause/backspace logic; add speed profiles and WPM display. |
| DuckScript parser | EvilDuck-S3 | MIT license; extract macro interpreter. |
| Snippet CRUD / file UI | EvilDuck-S3 | Adapt script storage to snippet storage. |
| Keyboard layouts | blue_keyboard | Study layout tables; implement US + UK first. |
| BLE fallback (future) | blue_keyboard + ESP32-BLE-Keyboard | Keep architecture modular so BLE can be added later. |

## Open Research Questions
- Does konkop's code compile cleanly with the latest `espressif32` PlatformIO platform?
- What is the maximum stable HID typing speed for ESP32-S3 on Windows/macOS/Linux?
- How do different OSes handle very fast USB HID key reports? (Need real-hardware testing.)
- Which exact ESP32-S3 board will the user use? (DevKit, T-Dongle, etc.)
