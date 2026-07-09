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

## Key Features (Planned)
- **WiFi AP + web UI** — works with any phone browser.
- **Huge paste support** — phone streams text to the ESP32 in chunks, so size is limited by phone memory, not ESP32 RAM.
- **Speed modes with WPM readout** — Max Speed, Fast Typist (~100–120 WPM), and Human/Careful (~60–80 WPM).
- **Anti-detection typing** — configurable jitter, burst pauses, optional typo simulation.
- **Stored snippets** — save frequently used text on the device.
- **Simple macros** — DuckScript-like commands for delays, special keys, and modifiers.
- **Keyboard layout selector** — match the host OS layout.

## Documentation
- [`docs/research.md`](docs/research.md) — existing projects and what we can reuse.
- [`docs/decisions.md`](docs/decisions.md) — design decisions and rationale.
- [`docs/hardware.md`](docs/hardware.md) — recommended boards and wiring.
- [`docs/plan.md`](docs/plan.md) — full implementation plan (or see the approved Kimi plan file).

## Status
Working on real hardware. USB HID mode (ESP32-S3) is functional: Wi-Fi AP, web UI, chunked text paste, mouse jiggler, layout selector, and OTA firmware updates have been verified.

See [`docs/lessons-learned.md`](docs/lessons-learned.md) for the bugs and hardware quirks discovered during bring-up.

## Roadmap / TODO

- [ ] **HID brand cloning** — make the device advertise as different keyboard vendors (Logitech, Apple, etc.).
- [ ] **Web UI polish** — fix layout/usability issues and add live status information.
- [ ] **Typing ETA in web UI** — show estimated completion time based on speed mode and remaining characters.
- [ ] **Huge paste support** — reliably paste 6000+ characters without stalling or dropping data.
- [ ] **Ultimate mouse jiggler** — 1-pixel micro-movements, human-like jitter, work-hours schedule, smart pause during paste, and a real-time status panel.
- [ ] **Multi-language paste support** — handle non-ASCII input such as Korean, emoji, and other scripts.

## License
See individual source files. We plan to reuse MIT-licensed code from konkop and EvilDuck with clear attribution.
