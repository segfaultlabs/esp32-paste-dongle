# ESP32 Paste Dongle

Plug an ESP32-S3 into a computer's USB port. Join its Wi-Fi AP from your phone. Open the web UI, paste any text, hit **Type it** — the dongle types it into whatever window is focused on the computer.

No app, no cloud, no drivers, no network access required on the host PC.

---

## What It Does

- **Paste engine** — streams text from phone to host in 512-byte chunks with real backpressure. Large pastes (tested to 100 kB+) work reliably.
- **Speed modes** — Max Speed, Fast Typist (~120 WPM), Human/Careful (~70 WPM) with per-keystroke jitter, burst pauses, and optional typo simulation.
- **Non-ASCII fallback** — UTF-8 aware. Accented Latin, smart quotes, em dashes, `©`, `€`, `™` and 70+ other characters are automatically approximated to ASCII equivalents. Unknown characters are counted and reported after paste.
- **Snippets** — 8 NVS-backed text slots. Save, edit, preview, and type with one tap. Survive reboots.
- **Macros** — DuckyScript-like scripting: `STRING`, `DELAY`, `ENTER`, `TAB`, `F1–F12`, arrow keys, `MOD_KEY CTRL c`, etc. Runs on device; targets focused window.
- **Mouse jiggler** — keeps cursor alive to prevent screen lock and idle timeouts.
  - **Natural mode** (default): Poisson-distributed intervals (no detectable heartbeat), Ornstein-Uhlenbeck cursor drift with Gaussian noise — designed to be undetectable by anti-idle software.
  - Geometric modes (random/line/square/circle) retained for compatibility.
- **Human simulation** — ambient "look busy" typing: types random word bursts and erases them in a loop. Only runs when paste/macro engine is idle.
- **HID brand cloning** — device presents itself to the host as any USB keyboard or mouse. 8 presets: Logitech K380, MX Keys, MX Master 3, Apple Magic Keyboard, Magic Mouse 2, Dell KB216, G502, or fully custom VID/PID/strings. Takes effect on next reboot.
- **OTA firmware updates** — upload `.bin` over Wi-Fi from the web UI or `scripts/ota_upload.py`. Dual-OTA partition table with automatic rollback after 60 s.
- **LittleFS web UI** — the web interface lives in the device filesystem, not embedded in firmware. Update it with `python3 scripts/upload_ui.py` — no firmware reflash, no serial port, no bootloader mode.
- **Keyboard layout** — US, UK, Dvorak. Wrong layout = wrong characters, so it checks and warns.
- **Onboard RGB LED** — status indicator: white breath (booting), cyan pulse (waiting for USB host), mint (idle), fast mint pulse (typing), blue (jiggling), orange (macro), purple (simulation), red (error).
- **mDNS** — reachable at `paste.local` in addition to `192.168.4.1`.
- **Serial CLI** — `name`, `getname`, `type`, `jiggle on/off/cfg/status`, `reboot`, `help`.

---

## Hardware

**Recommended**: ESP32-S3-DevKitM-1 or DevKitC-1 (any variant with 4 MB flash and onboard RGB LED on GPIO 48).

USB HID mode requires native USB-OTG (ESP32-S3). BLE HID mode works on classic ESP32 but is less tested.

---

## Quick Start

1. Flash the firmware (see below).
2. Upload the web UI filesystem: `python3 scripts/upload_ui.py`
3. Connect your phone to the **PasteDongle** Wi-Fi AP (password: `pastepaste`).
4. Open `http://192.168.4.1` (or `http://paste.local`).
5. Paste text, focus a window on the host, tap **Paste it**.

---

## Build & Flash

```bash
# First flash (sets up partition table — serial required once)
pio run -e esp32s3-usb -t upload

# Upload web UI to device filesystem
python3 scripts/upload_ui.py

# All subsequent firmware updates over Wi-Fi
python3 scripts/ota_upload.py .pio/build/esp32s3-usb/firmware.bin

# All subsequent UI updates over Wi-Fi (no firmware reflash)
python3 scripts/upload_ui.py
```

If you change the partition table, you must serial-flash all three images:
```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem* --baud 921600 write_flash -z \
  0x0     .pio/build/esp32s3-usb/bootloader.bin \
  0x8000  .pio/build/esp32s3-usb/partitions.bin \
  0xE000  /tmp/otadata_blank.bin \
  0x10000 .pio/build/esp32s3-usb/firmware.bin \
  0x170000 .pio/build/esp32s3-usb/firmware.bin
```

---

## Architecture

```
src/
  main.cpp            — setup(), loop(), WiFi AP, HTTP routes, WebSocket, OTA, serial CLI (~1800 lines, split planned)
  paster.cpp/h        — UTF-8 aware paste engine with backpressure and transliteration
  transliterate.cpp/h — UTF-8 decoder + 70-entry ASCII approximation table
  typing_engine.cpp/h — WPM timing, jitter, burst pauses, typo simulation
  keymap.cpp/h        — US / UK / Dvorak HID keycode lookup
  macro_parser.cpp/h  — DuckyScript-like script parser
  macro_runner.cpp/h  — tick-based macro executor with injectable clock
  snippet_store.cpp/h — 8-slot NVS snippet store
  mouse_jiggler.cpp/h — Poisson + Ornstein-Uhlenbeck jiggler + geometric modes
  human_sim.cpp/h     — ambient typing simulation (PAUSING→TYPING→ERASING)
  led_controller.cpp/h— NeoPixel status LED, 8 states
  config_store.cpp/h  — NVS settings cache (all reads from RAM, one write per change)
  hid/
    ihid_backend.h    — abstract HID interface
    usb_hid.cpp/h     — TinyUSB keyboard + mouse, VID/PID/name configurable
    ble_hid.cpp/h     — ESP32 BLE keyboard

data/
  index.html          — web UI (served from LittleFS, updated without reflash)

test/
  test_*.cpp          — 355 host-side unit tests (no Arduino/FreeRTOS required)
  Makefile

scripts/
  ota_upload.py       — Wi-Fi firmware upload
  upload_ui.py        — Wi-Fi UI filesystem upload
```

**WebSocket protocol**: all messages are JSON with a `t` field for type. Firmware sends `status`, `ack`, `jiggle_state`. Client sends `start`, `chunk`, `cancel`, `jiggle_get`, `jiggle_cfg`, `ping`.

**Partition table** (`partitions_4mb.csv`): NVS (20 kB) + OTA data (8 kB) + app0 (1.4 MB) + app1 (1.4 MB) + LittleFS (1.2 MB).

---

## Test Suite

```bash
cd test && make run
# 355 tests — paster, typing engine, keymap, macro parser, macro runner, human sim, transliterate
```

All tests run on the host (macOS/Linux) with injected clocks and mock backends. No device required.

---

## What's Still To Do

| # | Item | Notes |
|---|---|---|
| 1 | **Split `main.cpp`** | ~1800 lines — break into `wifi_ap`, `web_server`, `ws_handler`, `ota`, `serial_cli` modules. No behaviour change. |
| 2 | **Macro completion signal** | Macro runner finishes silently. Needs a `{"t":"macro_done"}` WS event so the UI can re-enable the Run button accurately. |
| 3 | **Version bump** | `"0.9.0"` hardcoded in `platformio.ini`. Should reflect real milestones. |
| 4 | **BLE parity** | BLE backend builds but hasn't been tested since the USB refactor. Layout, jiggler, and snippets may need verification. |
| 5 | **AP password first-boot generation** | Currently hardcoded `"pastepaste"`. Plan: random Diceware password on first boot, printed to serial + shown as QR code. Blocked until explicitly requested. |

---

## License

MIT. See individual source files for attribution where third-party code is included.
