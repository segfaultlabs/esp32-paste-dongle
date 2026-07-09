# ESP32 Paste Dongle

Paste text from your phone into any computer using an ESP32 as a keyboard proxy.

Works with two hardware modes:
- **USB HID mode** — ESP32-S3 plugs into the computer's USB port and acts as a wired keyboard.
- **BLE HID mode** — Classic ESP32 pairs with the computer over Bluetooth and acts as a wireless keyboard.

## What It Does
- Connect your phone to the ESP32's WiFi access point.
- Open the built-in web page, paste your text, and hit **Type it**.
- The ESP32 sends the text as keystrokes to the computer (via USB or Bluetooth, depending on firmware).

No app install, no cloud, no e-mail, no network required on the host PC.

## Why
Useful when you need to move large blocks of text (passwords, config files, scripts, log snippets) from your phone to a computer that can't or shouldn't receive them over the network.

## Key Features
- **WiFi AP + web UI** — works with any phone browser.
- **Chunked text paste** — phone streams text to the ESP32 so size is limited by phone memory, not ESP32 RAM.
- **Speed modes with WPM readout** — Max Speed, Fast Typist (~120 WPM), and Human/Careful (~70 WPM).
- **Anti-detection typing** — configurable jitter, burst pauses, typo simulation.
- **Keyboard layout selector** — match the host OS layout (US, UK, Dvorak).
- **Mouse jiggler** — basic patterns and interval randomization.
- **OTA firmware updates** — update over Wi-Fi without unplugging the board.
- **Serial CLI** — configure name, jiggler, and typing over USB serial.

## Documentation
- [`docs/research.md`](docs/research.md) — existing projects and what we can reuse.
- [`docs/decisions.md`](docs/decisions.md) — design decisions and rationale.
- [`docs/hardware.md`](docs/hardware.md) — recommended boards and wiring.
- [`docs/plan.md`](docs/plan.md) — full implementation plan (or see the approved Kimi plan file).

## What Works Now (v0.1)

USB HID mode on the ESP32-S3 is functional and has been tested on real hardware:

- Wi-Fi AP with captive-style web UI
- Chunked text paste from phone to host PC
- Speed modes and anti-detection typing (jitter, bursts, typos)
- Host layout selector (US / UK / Dvorak)
- Basic mouse jiggler
- OTA firmware updates over Wi-Fi
- USB serial console for debugging and configuration

See [`docs/lessons-learned.md`](docs/lessons-learned.md) for the bugs and hardware quirks discovered during bring-up.

## What's Still Missing / Needs Work

This is a working prototype, not a polished product. Honest gaps:

- **Web UI is bare-bones** — functional but ugly, with no live ETA, no progress indicators for huge pastes, and a reconnecting WebSocket that flickers.
- **No support for non-ASCII text** — Korean, emoji, and most non-Latin scripts cannot be pasted. The dongle only knows USB HID keycodes for Latin layouts.
- **Huge pastes are unreliable** — 6000+ characters can stall or fail; the backpressure between phone, WebSocket, and HID report rate has not been tuned.
- **Mouse jiggler is obvious** — geometric patterns and fixed-distance jumps are easy to detect. It needs micro-movements, human-like jitter, and scheduling.
- **No HID brand cloning** — the device advertises as a generic ESP32 HID device, which stands out next to real Logitech/Apple/Dell keyboards.
- **BLE mode is unverified** — the codebase builds for BLE but recent testing focused on USB HID.
- **No stored snippets or macros** — despite being in the original plan, these are not implemented.
- **No human simulation typing** — only static paste and a basic jiggler exist; there is no "look busy" typing mode.
- **Onboard LED is unused** — no status indication from the device itself.
- **OTA uses multipart only** — plain `curl --data-binary` does not work because of how AsyncWebServer handles uploads.
- **No security beyond the default AP password** — anyone on the `PasteDongle` network can control the device.

## Roadmap / TODO

- [ ] **HID brand cloning** — make the device advertise as different keyboard vendors (Logitech, Apple, etc.).
- [ ] **Web UI polish** — fix layout/usability issues and add live status information.
- [ ] **Typing ETA in web UI** — show estimated completion time based on speed mode and remaining characters.
- [ ] **Huge paste support** — reliably paste 6000+ characters without stalling or dropping data.
- [ ] **Ultimate mouse jiggler** — 1-pixel micro-movements, human-like jitter, work-hours schedule, smart pause during paste, and a real-time status panel.
- [ ] **Human simulation typing** — automatically type random text or a chosen blurb, then backspace/delete it, repeating to simulate active work.
- [ ] **Onboard LED control** — configure the ESP32-S3 onboard RGB/Neopixel LED from the web UI (status, brightness, patterns).
- [ ] **Multi-language paste support** — handle non-ASCII input such as Korean, emoji, and other scripts.

## License
See individual source files. We plan to reuse MIT-licensed code from konkop and EvilDuck with clear attribution.
