# Design Decisions

This document records the decisions made for the ESP32 paste-dongle and the rationale behind each one.

## 1. Connectivity: WiFi AP + Web UI

**Decision:** The ESP32 creates its own WiFi access point. The user connects a phone to that AP and opens a web page served by the ESP32.

**Rationale:**
- No native app required on the phone.
- Works on any phone with a browser.
- Higher bandwidth than BLE, which is important for large text/code blocks.
- Avoids the limited browser support of WebBLE on iOS.
- Avoids the complexity of BLE pairing and bonding.

**Trade-offs:**
- The ESP32 broadcasts an SSID.
- Power consumption is higher than BLE.
- Range is shorter than BLE in some environments.

**Rejected alternatives:** BLE + native app, BLE + WebBLE, WiFi station mode (join existing network).

## 2. Security Model: Basic Prototype (WPA2 AP + HTTP)

**Decision:** The AP uses WPA2 with a known password. Traffic between phone and ESP32 is unencrypted HTTP.

**Rationale:**
- This is a prototype for personal/trusted-environment use.
- The attack surface is limited: an attacker must be within WiFi range and know the AP password.
- Adding TLS with a self-signed certificate creates browser warnings and friction.

**Trade-offs:**
- Traffic is not encrypted end-to-end inside the AP.
- Not suitable for hostile networks or leaving the AP enabled in public spaces.

**Future options:** HTTPS with self-signed cert, device PIN auth, hidden SSID, BLE fallback.

## 3. Hardware: ESP32 (BLE) and ESP32-S3 (USB + BLE)

**Decision:** Support two hardware targets:
- **ESP32 (classic)** — uses BLE HID keyboard output to the target PC.
- **ESP32-S3** — uses USB HID keyboard output (and optionally BLE HID output) to the target PC.

**Rationale:**
- The user has classic ESP32 boards available now and wants to use them.
- The ESP32-S3 has a built-in USB-OTG peripheral for USB HID.
- Classic ESP32 has BLE and can act as a BLE HID keyboard using ESP32-BLE-Keyboard.
- A unified codebase lets the user choose the firmware/software variant for their hardware.

**Candidate boards:**
- **ESP32-DevKitC** (classic) — for BLE-only mode.
- **ESP32-S3-DevKitC-1** — for USB HID mode (and BLE mode).
- **LilyGO S3 T-Dongle** — compact USB dongle form factor for USB HID mode.

## 4. Framework: Arduino + PlatformIO

**Decision:** Build on the Arduino-ESP32 core using PlatformIO.

**Rationale:**
- Largest ecosystem of libraries and examples.
- Konkop's reference project already uses this stack (MIT license).
- Easier to iterate than raw ESP-IDF.
- PlatformIO provides reliable dependency management and upload tools.

**Rejected alternative:** ESP-IDF (more control but slower development), MicroPython/CircuitPython (easier to write but slower runtime and limited memory).

## 5. HID Output Backend Architecture

**Decision:** Abstract the keyboard output behind a common `IHidBackend` interface with two implementations:
- `UsbHidBackend` — for ESP32-S3 USB-OTG HID.
- `BleHidBackend` — for ESP32 (and ESP32-S3) BLE HID using ESP32-BLE-Keyboard.

**Selection method:** Compile-time build flag (`-D HID_BACKEND_USB` or `-D HID_BACKEND_BLE`). The same source code builds different firmware variants via PlatformIO environments.

**Rationale:**
- Keeps the typing engine, web UI, snippets, and macros identical across hardware.
- Lets the user "choose in software" by flashing the appropriate firmware variant.
- On ESP32-S3, either backend can be selected; on classic ESP32, only BLE is available.

**Future enhancement:** On ESP32-S3, allow runtime switching between USB and BLE via the web UI.

## 6. Web Server: ESPAsyncWebServer + WebSocket

**Decision:** Use `ESPAsyncWebServer` with `AsyncTCP` and a WebSocket endpoint for real-time status and chunked text transfer.

**Rationale:**
- Async server handles multiple connections without blocking the typing loop.
- WebSocket gives us bidirectional, low-latency status updates.
- Chunked paste over WebSocket avoids HTTP body size limits and simplifies flow control.

## 7. File System: LittleFS

**Decision:** Use LittleFS for storing the web UI and user snippets.

**Rationale:**
- More resilient than SPIFFS for frequent writes.
- Good support in Arduino-ESP32.
- Adequate for storing HTML/JS/CSS and small snippet files.

## 8. Handling Large Text: Phone-Side Streaming

**Decision:** The phone keeps the entire paste in browser memory and sends it to the ESP32 in chunks. The ESP32 types one chunk and requests the next.

**Rationale:**
- ESP32 RAM (typically ~320 KB usable) is too small to buffer very large pastes.
- Modern phones can easily hold multi-megabyte clipboard text in browser memory.
- Chunking with acks provides natural flow control.

**Chunk size:** Start with 256–512 bytes and tune based on reliability.

## 9. Speed Modes

**Decision:** Provide three speed modes with WPM readout and estimated completion time.

| Mode | Approx. WPM | Behavior |
|---|---|---|
| Max Speed | As fast as USB/HID allows | No jitter; raw paste |
| Fast Typist | ~100–120 WPM | Light jitter, micro-pauses |
| Human / Careful | ~60–80 WPM | Per-key jitter, burst pauses, optional typo simulation |

**Rationale:**
- "Max Speed" is essential for moving huge blocks of code/logs quickly.
- "Fast Typist" balances speed with plausibility.
- "Human" mode reduces the chance of host input-rate limiting or suspicion.

## 10. Anti-Detection / Human-Like Typing

**Decision:** Implement configurable jitter, burst pauses, and optional typo simulation.

**Rationale:**
- Some hosts or applications drop characters or flag perfectly uniform timing as automation.
- Human-like delays make the device usable in scenarios where very fast input looks suspicious.
- All jitter is configurable and can be disabled for Max Speed mode.

## 11. Macros: Minimal DuckScript-Like Syntax

**Decision:** Support a small subset of DuckScript: `STRING`, `DELAY`, `ENTER`, `TAB`, and modifier combinations.

**Rationale:**
- DuckScript is a de facto standard in the HID injection community.
- EvilDuck-S3 has a full MIT-licensed interpreter we can adapt.
- Macros add power-user value without much extra UI complexity.

## 12. Snippet Storage

**Decision:** Store user-defined snippets/macros in LittleFS and expose CRUD endpoints in the web UI.

**Rationale:**
- Persistence across reboots.
- One-tap paste for frequently used text.
- Reuses EvilDuck-S3 patterns for file upload/run/stop.

## 13. Reuse Strategy

**Decision:** Copy and adapt MIT-licensed open-source code where possible, with clear attribution.

**Rationale:**
- The user explicitly wants to avoid coding everything from scratch.
- Several high-quality reference projects exist.
- MIT licenses permit reuse with attribution.

**Primary sources:**
- konkop/ESP32-S3-USB-HID-Macro-Keyboard-Web-Typing-Interface
- cifertech/EvilDuck-S3
- larrylart/blue_keyboard (for study/reference)
- Arduino-ESP32 USBHIDKeyboard examples

## 14. Keyboard Layouts

**Decision:** Support a selectable keyboard layout in the UI, starting with US and UK.

**Rationale:**
- HID key reports are scancodes; the host OS maps them to characters based on the active keyboard layout.
- If the dongle sends US scancodes but the host expects UK, characters like `@`, `"`, `#`, `£` will be wrong.
- A layout selector lets the user match the dongle output to the host layout.

**Future layouts:** DE, FR, ES, Nordic, etc.

## 15. Target OS

**Decision:** Target Windows, macOS, and Linux.

**Rationale:**
- USB HID keyboards are OS-agnostic.
- Testing is required on all three because keyboard layout handling and input-rate limits differ.

## 16. Power-User Scope

**Decision:** Build a tool with core paste, stored snippets, and simple macros from the start.

**Rationale:**
- The user wants a practical tool, not just a one-off demo.
- Snippets and macros are lightweight additions that add significant utility.

**Deferred features:** Runtime switching between USB and BLE on ESP32-S3, OLED UI, captive portal, advanced security, multi-language layouts beyond US/UK.
