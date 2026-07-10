# ESP32 Paste Dongle — Recommendations & Path to a Mature v1.0

## Context

The ESP32 Paste Dongle is a working v0.1 prototype that turns an ESP32-S3 (USB HID) or classic ESP32 (BLE HID) into a phone-controlled keyboard proxy. A phone joins the device's Wi-Fi AP, opens a web page, and pastes text that the ESP32 types into the host PC.

Reading the codebase against the README's "what's missing" list and the design docs (`docs/plan.md`, `docs/decisions.md`), the prototype is functional but has real correctness bugs, architectural sprawl, and a bare-bones UI. This document names each flaw, ranks it, and specifies a v1.0 that is *simple to use* and *rich to hold*: a stunning single-page web app backed by a small, testable firmware core.

The goal is not just a longer feature list — it is to make the *first minute* of use feel magical (join AP → paste → done) while unlocking power-user depth (snippets, macros, layouts, human simulation, brand cloning) behind progressive disclosure.

---

## Part 1 — Correctness Bugs & Architectural Flaws

These are ordered by severity and blast radius. Anything marked **BUG** produces wrong behavior in the current firmware.

### 1.1 Unbounded paste buffer → OOM on large text  (BUG, severity: high)
`Paster::feed()` unconditionally appends every incoming chunk to `buffer_` with no cap (`src/paster.cpp:31`). The web UI ACKs each chunk *immediately after buffering* (`src/main.cpp:480`), so the phone floods 512-byte chunks as fast as WebSocket delivers them. If the typing engine runs at 120 WPM (~10 chars/s) but chunks arrive at 100 kB/s, the buffer grows without bound. This is the root cause of "6000+ characters stall or fail" in the README.

**Fix:**
- Introduce a high-water mark on `buffer_` (e.g. 4 kB) and only ack the incoming chunk once the buffered pending count drops below a low-water mark. Real backpressure, not fake.
- Track `bytes_acked` separately from `bytes_typed`. The client waits for `ack|<bytes_acked>` and only advances its stream cursor when the device signals it wants more.
- Alternately, drop chunk-based streaming and use a WS binary frame with sequence numbers.

### 1.2 Concurrent access to `Paster`/`Jiggler` with no synchronization  (BUG, severity: high)
`ESPAsyncWebServer` runs handler callbacks on the AsyncTCP task, not the main loop. `handle_ws_message()` mutates `g_paster->feed()`/`cancel()` and `g_jiggler->set_settings()` while `loop()` reads `g_paster->tick()` and `g_jiggler->tick()`. Same for HTTP handlers touching `cfg`. There is no mutex. On dual-core ESP32-S3 this is a data race; symptoms include truncated pastes, torn `Status`, and occasional crashes on large paste.

**Fix:**
- Wrap `Paster` and `Jiggler` public methods with a `portMUX_TYPE` critical section, or move all mutation onto the main loop via a lock-free command queue (`freertos/queue.h`). The command-queue approach is cleaner: WS handler enqueues `Cmd::Feed{...}`, main loop drains the queue between `tick()`s.

### 1.3 `UsbHidBackend::is_connected()` is a lie  (BUG, severity: high)
Returns `true` unconditionally after `begin()` (`src/hid/usb_hid.cpp:31-35`). The paster therefore starts typing before the host has actually enumerated the HID interface. On a cold plug-in this drops the first N keystrokes.

**Fix:**
- Query `USBHIDDevice::ready()` or the underlying TinyUSB state (`tud_hid_ready()`, `tud_mounted()`).
- Add a `waiting_for_host` state in `Paster` and surface it in the UI ("Plug into your PC and wait for enumeration…").

### 1.4 `send_report` blocks the entire loop  (BUG, severity: medium)
`UsbHidBackend::send_report()` calls `delayMicroseconds(500)` between press and release (`src/hid/usb_hid.cpp:41-45`). During a 100 kB paste at 120 WPM that is 500 µs × 100 000 = 50 seconds of blocking micro-delays — which also starves the async web server and the WDT.

**Fix:**
- Split the keystroke into two `tick()`s (press this tick, release next tick) or use FreeRTOS `vTaskDelay(1)` to yield.
- On the release path, use the report-descriptor's roll-over behavior and just send an empty report without a per-report micro-delay.

### 1.5 `typing::Engine` overrides good randomness  (BUG, severity: medium)
Constructor calls `std::srand(std::time(nullptr))` (`src/typing_engine.cpp:40`), overwriting the `randomSeed(esp_random())` seeded in `setup()`. Worse, `std::time()` on ESP32 without an RTC returns 0, so every boot uses the same seed. Also `now_ms()` fallback uses `std::time()*1000` — same 0 issue.

**Fix:**
- Remove the `std::srand` call. Seed `std::rand` once from `esp_random()` in `setup()`.
- Use `esp_random()` directly for jitter (higher quality than `rand()`).

### 1.6 `Preferences` opened per getter/setter  (perf + wear leveling)
Every one of the ~10 `Store` getters/setters in `config_store.cpp` calls `prefs.begin(...); prefs.end();`. On boot this fires 5+ times; on every `/api/jiggler` POST it can fire 5 more. NVS transactions are expensive and each open/close reads sector metadata.

**Fix:**
- Cache values in a `Settings` struct in RAM; `save()` writes the whole struct at once.
- Batch writes: keep one `Preferences` open for the session, only close on shutdown.

### 1.7 OTA endpoint lacks auth + leaks on error  (security + memory bug)
- Anyone on the `PasteDongle` AP can flash arbitrary firmware over `/api/update` — no token, no signature check. Default AP password is a static string `"pastepaste"` in code (`src/config_store.cpp:42`).
- `request->_tempObject = new bool(false);` never gets freed on request abort (only on the success path). Memory leak per failed upload.

**Fix:**
- Require a bearer token (random per-device on first boot, shown once via serial + on device screen if present, storable in NVS).
- Use a smart pointer or `request->onDisconnect` to free `_tempObject`.
- Sign firmware with an Ed25519 key at build time; embed the public key in the running firmware; verify signature *before* `Update.end(true)`.

### 1.8 `main.cpp` is an 835-line god-object with 200 lines of embedded HTML/CSS/JS
This is the biggest architectural drag. Iterating on the web UI requires a full firmware rebuild + flash. There is no separation between:
- Wi-Fi/AP bring-up
- HTTP route wiring
- WS message handling
- Serial CLI parsing
- OTA handling
- The 200-line `INDEX_HTML` PROGMEM string.

**Fix:**
- Move UI to `data/` and serve via LittleFS with a proper partition. Use `pio run -t uploadfs` to iterate on the UI without reflashing firmware.
- Split `main.cpp` into: `wifi_ap.{h,cpp}`, `web_server.{h,cpp}`, `ws_protocol.{h,cpp}`, `cli.{h,cpp}`, `ota.{h,cpp}`. `main.cpp` should be < 100 lines: setup, loop, glue.
- Build the UI as a Vite/esbuild project in `web/`, output to `data/`, gzip-compressed. Serve with `Cache-Control: max-age=31536000, immutable` and a hashed filename per build. Fall back to a tiny bootstrap HTML embedded in flash so the device is always accessible even if the filesystem is corrupted.

### 1.9 WebSocket protocol is unversioned pipe-delimited text
No `version` field. If we change the semantics of `status|...` fields the phone silently misparses. `state === 'done'` string comparisons will not survive translation to typed messages.

**Fix:**
- Move to JSON with a `t` (type) and `v` (version) field: `{"t":"status","v":1,"state":"typing","typed":123,"total":456,"pending":8,"wpm":118}`.
- Client rejects messages with unknown `v` and shows "please reload page" toast.

### 1.10 `UK` layout is claimed but not implemented  (BUG, documentation)
`docs/flashing.md` and README hint at UK layout, but `keymap::is_supported()` only accepts `US` and `DVORAK` (`src/keymap.cpp:176-178`). A `POST /api/layout layout=UK` returns 400. `BleHidBackend::set_layout()` isn't overridden at all.

**Fix:**
- Add UK, DE, FR, ES, IT, Nordic keymaps. Structure as data tables (arrays of `Report` keyed by codepoint), one per layout. Use a codegen script (`scripts/gen_keymaps.py`) that reads a machine-readable layout spec (kbdlayout.info's JSON export) so we don't handwrite scancodes.
- Have BLE backend honor the layout by *not* using `BleKeyboard::print` (which is OS-mapped), and instead send raw scancodes via `sendKeyReport`.

### 1.11 Non-ASCII characters silently dropped
`UsbHidBackend::send_char()` returns `false` for anything not in the keymap and moves on. A user pasting "café" gets "caf" with no warning.

**Fix:**
- Pre-flight the paste on the client: highlight every un-typeable code point before the user hits "Type it", and offer three fallbacks:
  1. **Alt-code injection** (Windows) — hold Alt, type numeric code on the pad, release.
  2. **Unicode-input mode** (macOS `⌃⌥U`, Linux `⌃⇧U`) — auto-detected per host layout.
  3. **Skip and count** — proceed but show a report of skipped chars.
- For emoji / Korean / true non-Latin, add a "compatibility" option that types the base transliteration when possible.

### 1.12 Mouse jiggler is trivially detectable
Fixed-distance geometric patterns (square/circle/line) are the exact fingerprint commercial anti-idle detectors look for. Fixed intervals (even with ±25%) show up in histograms. `random` pattern uses `random(-d, d+1)` where `d` is user-supplied and typically 2–20 pixels — visible cursor jumps.

**Fix:** see §2.5 (Ultimate Jiggler spec).

### 1.13 Macro parser is dead code
`src/macro_parser.{h,cpp}` exists with tests but is *never wired into main.cpp*. It also lacks: arrow keys, function keys, chained modifiers (`CTRL+ALT+DEL`), variables, loops, `REPEAT`.

**Fix:** either delete it, or finish wiring it into a proper `/api/macro/run` endpoint with the snippet store (§2.6).

### 1.14 Default AP password `"pastepaste"` (weak, static)
Anyone within Wi-Fi range who has read the source (public repo) can join and type into the host PC. **This is the most serious security bug because the device's whole job is to inject keystrokes.**

**Fix:**
- On first boot, generate a random 12-character password from a Diceware-style word list ("river-mango-shell-42").
- Display it on the device's OLED (if fitted) and print to serial. Persist in NVS.
- Print on a QR code that encodes `WIFI:S:PasteDongle-XXXX;T:WPA;P:...;` — phone camera scan joins the AP instantly.
- Add a physical "factory reset" long-press on the BOOT button to regenerate the password.

### 1.15 Reboot endpoint is unauthenticated and unconfirmed
`POST /api/reboot` is one HTTP call away. Anyone on the AP can reboot the device mid-paste.

**Fix:** require the same bearer token as OTA; require a `?confirm=1` query parameter.

### 1.16 No mDNS
User has to remember `192.168.4.1`. On networks where DHCP hands out a different range (station mode later) the address will change.

**Fix:** Advertise `paste.local` via `ESPmDNS`. Enable in the captive portal so the phone auto-redirects.

### 1.17 Partition table wrong for LittleFS + OTA
`platformio.ini` sets `board_build.partitions = min_spiffs.csv` but the plan calls for LittleFS *and* two OTA slots. `min_spiffs.csv` has 1.9 MB app + 200 kB SPIFFS with no OTA rollback slot.

**Fix:**
- Custom `partitions_4mb_ota_littlefs.csv`: two 1.5 MB app slots (OTA0/OTA1), one 900 kB LittleFS, one nvs. On failed boot, ESP32 auto-rolls back to the previous slot.
- Add `esp_ota_mark_app_valid_cancel_rollback()` after the new firmware runs for 60 s.

### 1.18 CORE_DEBUG_LEVEL=3 in production
Chatty on serial, adds flash size. Fine for dev; ship level=1.

**Fix:** two configs — `env:esp32s3-usb-dev` (level 3) and `env:esp32s3-usb-release` (level 1, stripped symbols).

### 1.19 Chunk ack races the WS close on cancel
When the user hits Cancel mid-paste, the JS marks `running = false` but the ack pump may still be in `waitAck()`. The next WS message will be interpreted as a stale ack. Minor UX bug.

**Fix:** on `cancelBtn.onclick`, reject the pending `ackResolve` promise and abandon the chunk loop cleanly.

### 1.20 Serial CLI reads until `\n` in `loop()` blocking
`Serial.readStringUntil('\n')` with default timeout (1000 ms) blocks the loop for up to a second when input arrives. On dual-core this is tolerable; on classic ESP32 it starves the AsyncTCP task.

**Fix:** non-blocking read with a line buffer.

### 1.21 Web UI shows "Bluetooth name" even in USB mode
`INDEX_HTML` unconditionally renders a "Bluetooth name" section (`main.cpp:54-60`), which is misleading on USB HID firmware.

**Fix:** the `/api/status` endpoint returns `{"backend":"usb"|"ble"}` and the UI conditionally renders sections.

### 1.22 `total_chars_` never updated after `start(-1)` streaming feeds
`Paster::start(total_chars)` is called once, then `feed()` calls do not update `total_chars_`. If the client streams without knowing the total (which is allowed — the API says -1 means "unknown"), the progress bar can't compute a percentage.

**Fix:** treat `total_chars_ = -1` as "indeterminate" in the UI (spinner instead of bar). Also let clients update the total mid-stream: `size|<newtotal>`.

---

## Part 2 — Spec for the v1.0 Beautiful UI

Design principles:

1. **Two-tap paste.** From "AP joined" to "typing complete", the user's happy path is: open URL → paste → tap Type. No account, no settings gate, no first-run modal.
2. **Progressive disclosure.** Everything advanced (jitter tuning, HID brand cloning, macros, keymaps) is one tap away, but hidden by default.
3. **Zero-flicker realtime.** Progress, WPM, and ETA update at 30 Hz without perceptible jitter.
4. **Every state is designed.** Not just idle/typing — also "waiting for host", "host disconnected mid-paste", "chunk stalled", "OTA in progress", "device rebooting".
5. **Feel expensive.** No default browser buttons, native selects, or `1990-input-outline`. Custom styled controls with proper focus/hover/active states, animation, and haptics via `navigator.vibrate` on mobile.

### 2.1 Visual language (concrete)

- **Palette:** neutral OLED-first dark theme (`#0a0a0f` base, `#12121a` panels, `#e8e8ff` text). Accent: an electric-mint `#5cffb4` for CTAs and progress. Warn: `#ffb45c`. Error: `#ff5c78`. Optional light mode toggle.
- **Type:** Inter Variable for UI, JetBrains Mono for the paste textarea. Both self-hosted from LittleFS (~50 kB subset).
- **Motion:** 180 ms cubic-bezier(0.2, 0.9, 0.2, 1) for state transitions; 60 fps CSS transforms only (no `top`/`left` animation).
- **Iconography:** Lucide (MIT), inline SVG, no icon-font.
- **Density:** 8-pt grid. Cards `border-radius: 14px`. Buttons min 44 × 44 px touch target.
- **Sound:** optional subtle "typed" click on completion, via WebAudio synthesized (no sample files).

### 2.2 Layout — Single-page app, three tabs

```
┌────────────────────────────────────────┐
│  ●  PasteDongle          v1.0   [⚙︎] [↻] │  ← header bar with backend LED, version, settings, reload
├────────────────────────────────────────┤
│                                        │
│   [ Paste ]  Snippets  Jiggler  Macros │  ← tab bar
│                                        │
│   ┌──────────────────────────────────┐ │
│   │ Text area (autofocus)            │ │
│   │                                  │ │
│   │                                  │ │
│   └──────────────────────────────────┘ │
│   1,247 chars · ~10.3 s @ Fast Typist  │  ← live stats
│                                        │
│   Speed [Max] [Fast ▸] [Human]         │  ← segmented control
│   Layout [US ▾]                        │
│                                        │
│   [   ⏎  Type it   ]  [ Cancel ]       │
│                                        │
│   ▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░  62% · 4.1 s left│  ← progress + ETA
│   Typing… 118 WPM                       │
│                                        │
└────────────────────────────────────────┘
```

The header LED reflects device state at a glance:
- solid mint: idle & host ready
- pulsing mint: typing
- solid amber: host disconnected
- solid red: error
- pulsing red: OTA in progress

### 2.3 The Paste tab (main flow)

- **Textarea:** monospace, syntax-aware line numbers, drag-and-drop file support (accepts `.txt`, `.md`, `.env`, `.pem`, `.log` — reads client-side, never uploaded).
- **Un-typeable char highlighter:** every code point missing from the active layout is underlined in warn-orange. Hover / long-press shows: "This character is not in US layout. Options: Alt-code (Win) · Skip · Change layout." Runs in the browser off the layout table fetched at page load.
- **Speed segmented control:** three options with tooltips containing the exact WPM and estimated jitter. Long-press "Human" opens **Advanced timing** modal (see §2.7).
- **Layout picker:** shows the OS-detected layout as a hint ("Your Mac says: US"). One tap changes both the UI and the device — no separate save button.
- **"Type it" button:** primary CTA, always visible, disabled when text is empty or backend is disconnected. On press, animates into a progress state (in-button progress ring around a live WPM counter).
- **Progress row:** progress bar, ETA (`4.1 s left`), live WPM, and pending-buffer indicator (small dot that pulses when phone is streaming, dims when it stops). This visualization directly answers the README's "the WebSocket flickers" complaint.
- **Post-completion:** the Type-it button flashes mint-check, plays optional tick sound, and clears the textarea only if the user opts in ("clear after paste" toggle).

### 2.4 The Snippets tab (planned feature — implement in v1.0)

- Vertical list of saved snippets, each with:
  - Title, first-line preview, character count, last-used timestamp.
  - Quick actions: "Type" (immediate paste), edit, delete, duplicate.
  - Optional tag chips (e.g. `#password`, `#config`, `#log`).
- Global "New snippet" button.
- Import/export as JSON.
- Full-text search box at the top (client-side fuzzy).
- Storage: LittleFS `/snippets/<uuid>.json`.

### 2.5 The Jiggler tab — "Ultimate mouse jiggler"

The bar to clear is Amphetamine / Caffeine-level polish plus better anti-detection than commercial jigglers.

Behaviours:
- **Sub-pixel micro-movements.** Instead of moving cursor by 2–20 px, move by 1 px in a random direction *at random intervals* (Poisson-distributed around a user-set mean). One pixel is invisible on modern monitors.
- **Idle-triggered.** Only jiggle after N seconds of true idleness. "True idle" = no keystrokes typed by the device recently and (if supported later) no user activity on the host relayed back over WebHID. Default 45 s.
- **Work-hours schedule.** UI has a weekly grid (7×24) where the user paints active hours. Outside of active hours, jiggler sleeps.
- **Smart pause during paste.** Automatically stops jiggling while `Paster::state == TYPING`.
- **Natural cursor drift.** Instead of geometric line/square/circle, generate a 2D random walk with slight momentum (Ornstein-Uhlenbeck process) so the cursor drifts like a barely-still hand. Return to origin over time.
- **Realtime status panel.** Live "next jiggle in 00:14", "last movement: 2 px SE", "today: 342 movements".
- **Zero-input mode.** Optionally, instead of moving the mouse, send a Shift-Shift no-op keystroke (some anti-idle systems accept this without moving the cursor).

Backend changes:
- Extend `Jiggler` state machine to Poisson intervals; add a `NaturalDrift` pattern.
- Add `scheduler` module with a `Schedule` type (7×24 bitmask).
- Persist all of the above in NVS.

### 2.6 The Macros tab (planned — implement)

- Editor with syntax highlighting for the DuckyScript-like syntax already parsed by `macro_parser.cpp`.
- Add: `REPEAT n`, arrow keys, function keys, chained modifiers, `WAIT_FOR_HOST_READY`, `RANDOM_DELAY min max`, `IMPORT_SNIPPET <id>`.
- "Run" button; "Dry-run" produces a preview of what would be typed and how long it would take.
- Version history (last 10 revisions kept in LittleFS).
- Warning banner if the macro contains destructive combos (`GUI+R`, `CTRL+ALT+DEL`, `CMD+Q`).

### 2.7 Advanced timing modal

Everything the README's "anti-detection" bullet promises, exposed here:

- **Base WPM** slider (30–600).
- **Per-key jitter** (±% of base).
- **Burst pause** (every N chars, pause min/max ms).
- **Word pauses** (extra 100–400 ms after `space`/`enter`).
- **Typo rate** (0–5%) with backspace-retype behavior.
- **First-line delay** (pause 200–800 ms before the first char, so the host has time to focus the field).
- **Fatigue** (WPM slowly declines over long pastes).
- **Live preview** — a small textarea that simulates typing with the current settings so the user can *see* the rhythm before hitting Type-it.

### 2.8 Settings drawer (gear icon)

- **Wi-Fi:** AP name, password (with regenerate button + QR code), hidden-SSID toggle.
- **Security:** rotate API bearer token, show/hide "unpaired keys" (physical button on device to arm).
- **Backend info:** USB vs BLE, host layout, connected/disconnected, uptime, free heap, firmware version, git SHA.
- **HID identity:** Device name, VID/PID pair (see §2.10 HID brand cloning).
- **LED:** onboard NeoPixel color/brightness/pattern per state.
- **OTA:** upload firmware, see current slot, "rollback to previous" button.
- **Factory reset:** two-tap confirm.

### 2.9 Onboard LED (planned — implement)

Use the ESP32-S3's built-in RGB NeoPixel:

| State | LED |
|---|---|
| Booting | soft white breath |
| AP up, waiting for host | pulsing cyan |
| Host connected, idle | solid mint (50% brightness) |
| Typing | mint chase, rate scaled to WPM |
| OTA in progress | pulsing violet |
| Error | solid red |
| Factory reset armed | fast red blink |

Add `led_controller.{h,cpp}` with a small pattern DSL so states can be tuned from the web UI without re-flashing.

### 2.10 HID brand cloning

The USBHIDKeyboard wrapper doesn't expose USB descriptors, but TinyUSB does. Add a `tusb_config.h` override to set VID/PID/manufacturer/product from NVS at boot.

Preset profiles (verify VID/PIDs are legal to advertise — see §4 risks):
- `Generic`
- `Logitech K380`
- `Apple Magic Keyboard`
- `Dell KB216`
- `Custom` (user enters VID/PID/name)

Requires reboot to apply.

### 2.11 "Human simulation" typing mode

Distinct from the paste flow — an ambient "look busy" mode.

- User picks a text source: Lorem, a chosen snippet, code sample, or "type-and-delete random paragraphs from a wordlist".
- Configurable duty cycle ("type 30 s, pause 15 s, repeat").
- Runs until stopped; occasional backspace-delete-all-and-restart.
- Interlocks with jiggler and real paste (both take priority; sim pauses).

### 2.12 First-run experience

- Phone joins AP → captive-portal DNS redirects any request to `192.168.4.1`.
- The device serves a 60 kB gzipped SPA. First paint < 500 ms.
- Welcome card: "Hi. Paste text below to send it to your PC. Layout: US. Change?" — one gentle nudge, dismissible.
- No modal, no login, no consent form.

### 2.13 Accessibility, i18n, PWA

- **A11y:** WCAG AA contrast, aria-labels on every icon button, focus rings visible on keyboard nav, `prefers-reduced-motion` disables non-essential motion.
- **i18n:** English + a JSON translation file loaded async. Ship EN, KO, DE, JA, ES.
- **PWA:** add `manifest.json` and a service worker so the user can install it as an app on their phone. Bonus: web share target so the OS "Share" sheet can send text directly.

### 2.14 Telemetry (privacy-preserving)

Local-only. A "Diagnostics" panel shows the last 100 events with timestamps: chunk received, chars typed, disconnect, error. Downloadable as JSON. Nothing leaves the device.

---

## Part 3 — Implementation Roadmap

Six focused milestones. Each ships something the user can hold.

### M0 — Foundation & bug purge (1 week)
1. Custom partition table with dual OTA + LittleFS (§1.17).
2. Split `main.cpp` into modules (§1.8) *without* changing behavior.
3. Wire real `is_connected()` for USB (§1.3).
4. Add lock-free command queue between WS handler and main loop (§1.2).
5. Bound the paste buffer with true backpressure (§1.1).
6. Fix `std::srand`/`std::time` bugs (§1.5).
7. Cache `Preferences` (§1.6).
8. Bearer token on `/api/update` and `/api/reboot` (§1.7, §1.15).
9. Non-blocking serial CLI (§1.20).
10. JSON-versioned WS protocol (§1.9).

Ship criterion: all existing features work identically, 100 kB paste succeeds cleanly, no data races in TSan (if run on Linux port).

### M1 — LittleFS-served SPA scaffold (1 week)
1. `web/` Vite + Preact (~10 kB gzipped) + Tailwind stripped-down.
2. Build script writes to `data/` gzipped.
3. Custom captive portal.
4. mDNS `paste.local` (§1.16).
5. Bootstrap fallback HTML in flash if LittleFS empty.
6. First-boot random AP password + QR code (§1.14).

Ship criterion: user can iterate on UI without reflashing firmware.

### M2 — Beautiful Paste tab (1 week)
- Everything in §2.1–§2.3 and §2.7.
- Live ETA, WPM, backpressure dot.
- Un-typeable char highlighter.
- Layout picker with OS hint.
- Post-completion animation.

### M3 — Snippets, Macros, LED, Human-sim (2 weeks)
- Snippet CRUD backed by LittleFS.
- Macro editor + `Runner` module with `REPEAT`/arrows/Fn keys.
- Onboard NeoPixel controller.
- Ambient typing simulator.

### M4 — Ultimate Jiggler (1 week)
- Poisson intervals, Ornstein-Uhlenbeck drift, work-hours grid.
- Idle detector.
- Smart pause during paste.
- Realtime status panel.

### M5 — HID brand cloning + multi-layout + non-ASCII fallbacks (2 weeks)
- TinyUSB descriptor override, preset profiles.
- Codegen'd keymaps for US/UK/DE/FR/ES/IT/Nordic/JP/KR.
- Alt-code / Unicode-input fallbacks for non-ASCII.
- BLE parity: raw-scancode path for layout support.

### M6 — Polish, PWA, docs, release (1 week)
- Service worker + manifest.
- i18n (EN/DE/KO/JA/ES).
- Signed firmware releases via GitHub Actions.
- Screenshots, GIFs, README rewrite (v1.0), CHANGELOG.

Total: ~9 weeks part-time.

---

## Part 4 — Risks & Trade-offs

- **HID brand cloning legality.** Advertising a Logitech VID/PID is a trademark issue if the device is distributed. For personal use it is generally fine, but the UI should say so; shipped firmware should default to a neutral "Generic HID Keyboard" and require an explicit opt-in for clones.
- **Signed OTA + rollback slot** halves usable flash. 4 MB modules will feel tight; recommend nudging users toward 8 MB variants in `docs/hardware.md`.
- **Preact + Tailwind** even minimal will land around 20–30 kB gzipped. Alternative: hand-written vanilla JS + CSS in ~15 kB, but at higher maintenance cost. Recommend Preact for maintainability.
- **BLE scancode support.** `ESP32-BLE-Keyboard` does expose `sendReport` but its HID descriptor is fixed. May need to fork or move to `NimBLE-HIDKeyboard`.
- **Anti-detection is an arms race.** Jiggler improvements outpace commercial detectors today but not forever. Make behaviours pluggable so the community can add patterns.
- **Streaming backpressure changes the WS protocol.** Existing clients (there aren't any yet, so this is cheap) break. Take the opportunity now while the surface is small.

---

## Part 5 — Verification

For every milestone:

1. **Unit tests** (host-side) for `Paster`, `Engine`, `Jiggler`, `KeyMap` — extend the existing custom harness or switch to Unity (bundled with PlatformIO).
2. **Integration test** (`test/ota_update_test.sh` style) that runs against a real board or QEMU: paste 100 kB, verify byte-perfect output on the host side (`xdotool key` capture on a headless Linux VM).
3. **Manual UI test matrix:** iOS Safari, iOS Chrome, Android Chrome, Android Firefox — every tab, every state.
4. **Fuzz test** the new JSON WS protocol with malformed frames; server must never crash.
5. **Load test:** 1 MB paste at Max Speed. Backpressure must keep buffer < 8 kB throughout.
6. **Security test:** try `POST /api/update` without token → 401. Try weak token → 401. Try to join AP without password → fail.

Ship criterion for v1.0: at least one 100 kB paste succeeds byte-perfect on macOS, Windows, and Linux, from an iPhone and an Android, using both USB and BLE backends.

---

## Part 6 — README v1.0 Rewrite

Structure to aim for:

1. **Header + one-sentence pitch + demo GIF** (paste on phone → shows up on host).
2. **What it does** — three sentences, no lists.
3. **Quick start** — 4 numbered steps, screenshots.
4. **Features grid** — icons + one-liners, 8 cards.
5. **Screenshots** — the SPA tabs, the LED, the QR-code first-boot.
6. **Hardware** — link to `docs/hardware.md`, one recommended board.
7. **Build & flash** — collapsible detail, link to `docs/flashing.md`.
8. **Roadmap** — checklist that mirrors this doc.
9. **Contributing / License / Attribution.**

Move the "honest gaps" block to `docs/status.md` — it belongs there once we're past v1.0.
