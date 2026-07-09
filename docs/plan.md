# ESP32 Paste-Dongle Design & Implementation Plan

## Goal
Build a small ESP32-based USB dongle that lets a phone paste large blocks of text into a computer that accepts only a standard USB keyboard. The computer requires no special software. The primary use case is moving clipboard-sized text (passwords, configs, scripts, log snippets) from a phone to a PC when e-mail, messaging, cloud sync, or network file transfer is unavailable or disallowed.

## Prior Art Researched
- **BluKeyborg / blue_keyboard** — ESP32-S3 BLE HID dongle with native Android/iOS/Linux apps and mTLS.
- **ToothPaste Dongle** — ESP32-S3 BLE HID dongle controlled via WebBLE browser app; encrypts keystrokes.
- **Gooshy** — Commercial BLE phone-to-dongle text/paste product.
- **konkop's ESP32-S3 project** — USB HID keyboard with WiFi AP and a web typing UI.
- **EvilDuck / WUD-Ducky / USB Nugget** — ESP32/ESP32-S2/S3 based WiFi HID injection tools with web interfaces and DuckyScript support.
- **ESP32-BLE-Keyboard** and **USBHIDKeyboard** Arduino libraries.

All viable reference projects converge on ESP32-S3 + USB-OTG HID. The user explicitly prefers WiFi AP + web UI for this build.

## Decisions from Clarification
| Topic | Choice |
|---|---|
| Connectivity | WiFi AP + captive/local web UI |
| Security model | Basic prototype: WPA2 AP + HTTP (traffic stays on local AP) |
| Text size | Clipboard-sized, up to available device memory, streamed/chunked |
| Scope | Power-user tool: paste + stored snippets + simple macro/script support |

## Recommended Hardware
- **ESP32 (classic)** — for BLE HID keyboard mode. No USB-OTG required; the ESP32 advertises as a BLE keyboard and the target PC pairs over Bluetooth.
- **ESP32-S3** — for USB HID keyboard mode (and optionally BLE mode). Has native USB-OTG plus BLE/WiFi.
- Optional: small OLED screen for status, push button for safe mode/reset, 3D-printed enclosure.

## Architecture

Two firmware variants share the same core:

### USB HID variant (ESP32-S3)
```
┌─────────────┐   phone STA → ESP32 AP     ┌─────────────────┐      USB HID
│   Phone     │  ─────────────────────────>  │   ESP32-S3      │  ───────────>  ┌──────────┐
│ (web browser)│   HTTP + WebSocket/SSE      │  Paste Dongle   │                │  Host PC  │
└─────────────┘                              │  (USB HID + AP) │                └──────────┘
                                             └─────────────────┘
```

### BLE HID variant (ESP32 or ESP32-S3)
```
┌─────────────┐   phone STA → ESP32 AP     ┌─────────────────┐      BLE HID
│   Phone     │  ─────────────────────────>  │   ESP32         │  ───────────>  ┌──────────┐
│ (web browser)│   HTTP + WebSocket/SSE      │  Paste Dongle   │                │  Host PC  │
└─────────────┘                              │  (BLE KB + AP)  │                └──────────┘
                                             └─────────────────┘
```

### Components
1. **WiFi Access Point** — ESP32 creates a dedicated network with a known SSID/password.
2. **HTTP/WebSocket Web Server** — Serves a single-page web UI and receives text/script payloads.
3. **HID Output Backend** — Abstract interface with two implementations:
   - `UsbHidBackend` — ESP32-S3 USB-OTG HID keyboard.
   - `BleHidBackend` — ESP32/ESP32-S3 BLE HID keyboard (ESP32-BLE-Keyboard).
4. **Keystroke Engine** — Converts UTF-8 text into HID key reports, handles layouts, modifiers, special keys, timing, and chunking.
5. **Snippet Store** — Persists user-defined snippets/macros in flash (SPIFFS/LittleFS).
6. **Macro Parser** — Minimal DuckyScript-like syntax for delays, special keys, and snippets.

## Data Flow
1. User connects phone to ESP32 AP.
2. Browser opens `http://192.168.4.1` (or captive portal).
3. Web UI loads from SPIFFS/LittleFS or embedded PROGMEM.
4. User pastes text or selects a stored snippet/macro.
5. Web UI keeps the full text in browser memory and sends it to the ESP32 in small chunks (e.g., 256–512 bytes) over WebSocket or HTTP POST.
6. ESP32 receives a chunk, converts it to HID key reports using the selected layout, and types it with the configured speed and jitter.
7. ESP32 requests the next chunk (or the UI pushes the next chunk on receipt of an ack/progress event).
8. UI receives progress/status messages (queued, typing, chunk N of M, done, error).

## Feature Set

### Core Paste
- Large text area with live character count and an estimated time-to-type readout based on the selected speed profile.
- Streaming / chunked delivery: the phone keeps the full text in browser memory and feeds it to the ESP32 in small chunks (e.g., 256–512 bytes) over WebSocket; the ESP32 types one chunk and acks it before the next is sent. This avoids exhausting ESP32 RAM regardless of paste size.
- "Type it" button starts sending as USB keystrokes.
- Cancel button to stop an active paste.

### Speed Modes (with WPM readout)
The UI exposes at least three clearly labeled speed modes, each displaying an approximate WPM and an estimated completion time:
- **Max Speed** — paste as fast as the USB HID stack and host will accept. Minimal or zero jitter. Use this for huge blocks of code/logs when detection is not a concern.
- **Fast Typist** — ~100–120 WPM with light jitter and occasional micro-pauses. Looks like a very fast human typist; good balance of speed and plausibility.
- **Human / Careful** — ~60–80 WPM with per-key jitter, burst pauses, and optional typo simulation. Use this when the input should look indistinguishable from a person typing.

### Anti-Detection / Human-Like Typing
To avoid looking like a naive script or hitting host input-rate limits, the keystroke engine applies configurable jitter:
- **Per-key jitter:** each key's inter-key delay is the base delay plus/minus a small random percentage.
- **Burst pauses:** after a configurable number of characters, insert a longer random pause (e.g., 50–300 ms) to mimic a human stopping to read or reposition fingers.
- **Typo simulation (optional):** occasionally "mistype" a character, backspace, and retype it, at a user-configurable frequency.
- All jitter values are exposed in an "Advanced" settings panel and can be disabled entirely for Max Speed mode.

### Stored Snippets
- Save, rename, delete snippets in flash.
- One-tap paste from saved list.
- Persistent across reboots.

### Simple Macros
- Minimal script syntax, e.g.:
  ```
  STRING Hello, world!
  DELAY 500
  ENTER
  STRING second line
  ```
- Support for common special keys: ENTER, TAB, ESC, arrows, modifiers (CTRL+A, ALT+F4, etc.).

### Keyboard Layout
- Selectable keyboard layout in UI (US, UK, DE, FR, etc.).
- Firmware maps Unicode/code points to HID key reports for the chosen layout.

### Status & Feedback
- Progress bar / keystroke counter in UI.
- LED or on-screen status: idle, typing, error.
- Optional WebSocket/SSE for real-time status.

## File Structure (PlatformIO)
```
 esp32_keyboard/
 ├── platformio.ini            # Multi-environment config (esp32-ble, esp32s3-usb, ...)
 ├── src/
 │   ├── main.cpp              # setup/loop, AP, web server wiring
 │   ├── wifi_ap.cpp/h         # AP configuration
 │   ├── web_server.cpp/h      # HTTP + WebSocket endpoints
 │   ├── hid/
 │   │   ├── ihid_backend.h    # Abstract HID backend interface
 │   │   ├── usb_hid.cpp/h     # USB HID backend (ESP32-S3)
 │   │   └── ble_hid.cpp/h     # BLE HID backend (ESP32 / ESP32-S3)
 │   ├── typing_engine.cpp/h   # Speed, jitter, WPM logic
 │   ├── keymap.cpp/h          # layout tables (US, UK, ...)
 │   ├── snippet_store.cpp/h   # LittleFS persistence
 │   ├── macro_parser.cpp/h    # simple script parser
 │   └── config.cpp/h          # runtime settings (AP creds, layout, speed)
 ├── data/
 │   └── index.html            # web UI (single page)
 └── README.md
```

## Technology Stack
- **Framework:** Arduino-ESP32 via PlatformIO.
- **USB HID:** `USBHIDKeyboard` library (ESP32-S3 only).
- **BLE HID:** `ESP32-BLE-Keyboard` library.
- **Web Server:** `ESPAsyncWebServer` + `AsyncTCP` for async HTTP and WebSocket.
- **File System:** LittleFS for web UI and snippet storage.
- **Build/Upload:** PlatformIO multi-environment builds; choose environment for target hardware.

## Reuse Strategy
We will not build everything from scratch. Where licenses permit, we will copy/adapt existing open-source work:
- **konkop/ESP32-S3-USB-HID-Macro-Keyboard-Web-Typing-Interface** (MIT) — borrow the overall PlatformIO project structure, the web-UI serving approach, the TinyUSB/USB HID setup, and the human-like typing logic (random delays, pauses, backspaces).
- **cifertech/EvilDuck-S3** (MIT) — reuse the DuckScript parser/interpreter for macros and the browser-based control panel patterns for snippet upload/run/stop.
- **larrylart/blue_keyboard** — study the keyboard layout/keymap system for non-US layouts and the BLE-to-HID state machine (useful if we later add BLE fallback).
- **ESP32-BLE-Keyboard / USBHIDKeyboard Arduino examples** — use as reference for HID key report construction and modifier handling.

All reused code will be clearly attributed in file headers and README.

## Implementation Phases

### Phase 1 — Minimal HID Proof of Concept (Both Backends)
- Set up PlatformIO with multiple environments:
  - `esp32-ble` — classic ESP32 with BLE HID keyboard output.
  - `esp32s3-usb` — ESP32-S3 with USB HID keyboard output.
- Implement the abstract `IHidBackend` interface.
- Implement `BleHidBackend` using ESP32-BLE-Keyboard; verify the ESP32 pairs as a BLE keyboard and types "Hello World!".
- Implement `UsbHidBackend` using USBHIDKeyboard; verify the ESP32-S3 enumerates as a USB keyboard and types "Hello World!".

### Phase 2 — WiFi AP + Static Web UI
- Start a soft-AP with SSID/password.
- Serve a simple `index.html` from PROGMEM or LittleFS.
- Add `/api/paste` HTTP endpoint that receives text and types it.

### Phase 3 — Robust Streaming & Anti-Detection Typing Engine
- Implement phone-side chunking so the full paste never has to fit in ESP32 RAM.
- Add ESP32-side chunk ack/request protocol over WebSocket.
- Add configurable typing speed and inter-key delay.
- Add anti-detection jitter: per-key delay variance, burst pauses, optional typo simulation, and speed profiles.
- Add layout-aware keymap for at least US and UK.
- Handle non-ASCII characters gracefully (skip or compose where possible).

### Phase 4 — Snippet Store & Macros
- Add LittleFS-based snippet CRUD endpoints.
- Update UI to list, save, edit, and paste snippets.
- Implement simple macro parser with `STRING`, `DELAY`, `ENTER`, `TAB`, modifier keys.

### Phase 5 — UI Polish & Status
- Build a responsive single-page web UI with progress, cancel, settings.
- Add WebSocket status channel (`/ws`) for real-time typing progress.
- Add LED/OLED status indicators if hardware supports it.

### Phase 6 — Testing & Refinement
- Test on Windows, macOS, Linux.
- Test with large text (10 KB, 50 KB, 100 KB, and 1 MB if feasible) to verify phone-side streaming and chunk acks.
- Tune delays and jitter for reliability across hosts.
- Verify anti-detection settings do not cause dropped characters on slow hosts.
- Write README with build/flash/use instructions.

## Testing Plan
- **Enumeration test:** Device appears as HID keyboard in OS device manager/system profiler.
- **Basic paste test:** Paste a short ASCII string into a text editor on each target OS.
- **Large paste test:** Paste 10 KB, 50 KB, 100 KB, and (if feasible) 1 MB of mixed text without loss or corruption, using phone-side streaming.
- **Special characters test:** Test TAB, ENTER, quotes, backslashes, and layout-specific characters.
- **Macro test:** Run a macro that opens a terminal and types a command.
- **Snippet persistence test:** Save snippets, reboot device, verify they load.
- **Cancel test:** Start a large paste and cancel it mid-stream.

## Open Questions / Future Enhancements
- Do we want a captive portal so phones auto-open the UI when joining the AP?
- Should the AP credentials be configurable via the web UI?
- Should we later add BLE mode as a fallback for phones that can't join the AP?
- Do we need non-US keyboard layouts beyond UK/DE/FR for the first release?

## Risks & Mitigations
| Risk | Mitigation |
|---|---|
| Host drops characters at high typing speed | Make inter-key delay configurable; default conservative; add flow control |
| Large text exhausts RAM | Stream in chunks from HTTP body; don't buffer entire payload |
| Different OS keyboard layouts mismatch | UI layout selector + keymap tables |
| WiFi AP is visible/accessible | Basic WPA2 password; note that this is a prototype security level |

## Success Criteria
- [ ] `esp32-ble` firmware pairs as a BLE HID keyboard and types text into Windows/macOS/Linux.
- [ ] `esp32s3-usb` firmware enumerates as a USB HID keyboard and types text into Windows/macOS/Linux.
- [ ] Phone can connect to AP, open web UI, and paste text that appears on the host PC.
- [ ] Device handles at least 100 KB of text reliably via phone-side streaming.
- [ ] UI offers at least three speed modes (Max Speed, Fast Typist, Human/Careful) with WPM readout.
- [ ] Snippets persist across reboots.
- [ ] Macros support `STRING`, `DELAY`, `ENTER`, and at least one modifier combo.
- [ ] Reuses existing open-source code where licenses permit, with attribution.
