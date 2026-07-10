# Rubber Ducky / DuckyScript Compatibility

The USB Rubber Ducky (Hak5) is the reference HID injection tool. This document maps its feature set against our current macro implementation and shows what's needed to achieve parity.

---

## What the Rubber Ducky does

### DuckyScript 1.0 (original, line-based)

| Command | Syntax | Purpose |
|---|---|---|
| `REM` | `REM comment text` | Comment — ignored at runtime |
| `STRING` | `STRING Hello world` | Type text literally, char by char |
| `DELAY` | `DELAY 500` | Pause N milliseconds |
| `DEFAULTDELAY` | `DEFAULTDELAY 100` | Insert N ms after every subsequent command automatically |
| `REPEAT` | `REPEAT 5` (on line after command) | Repeat the previous command N times |
| `ENTER` | `ENTER` | Press Enter/Return |
| `TAB` | `TAB` | Tab key |
| `SPACE` | `SPACE` | Space bar |
| `BACKSPACE` | `BACKSPACE` | Backspace |
| `DELETE` | `DELETE` | Forward delete |
| `INSERT` | `INSERT` | Insert key |
| `HOME` / `END` | `HOME` | Line start/end |
| `PAGEUP` / `PAGEDOWN` | `PAGEUP` | Page navigation |
| `UP/DOWN/LEFT/RIGHT` | `UP` | Arrow keys |
| `F1–F12` | `F1` | Function keys |
| `CAPSLOCK` | `CAPSLOCK` | Toggle Caps Lock |
| `NUMLOCK` | `NUMLOCK` | Toggle Num Lock |
| `SCROLLLOCK` | `SCROLLLOCK` | Toggle Scroll Lock |
| `PRINTSCREEN` | `PRINTSCREEN` | PrtSc / SysRq |
| `PAUSE` / `BREAK` | `PAUSE` | Pause/Break key |
| `MENU` | `MENU` | Application / right-click menu key |
| Modifier + key | `CTRL a` | Press key with modifier held |
| Multi-modifier | `CTRL SHIFT a` | Multiple modifiers simultaneously |
| `GUI` / `WINDOWS` | `GUI r` | Windows key / Cmd key |
| `ALT` | `ALT F4` | Alt key |
| `SHIFT` | `SHIFT F10` | Shift key |

### DuckyScript 3.0 additions (programmable)

| Feature | Syntax | Purpose |
|---|---|---|
| Variables | `VAR $name = "value"` | Store strings/numbers |
| String interpolation | `STRING Visit ${url}` | Embed variables in text |
| `IF/ELSE` | `IF ($x == "y") { ... } ELSE { ... }` | Conditional execution |
| `WHILE` | `WHILE ($i < 5) { ... }` | Loops |
| `FOR` | `FOR (VAR $i = 0; $i < 5; $i++) { ... }` | Counted loops |
| `FUNCTION` | `FUNCTION name() { ... }` | Reusable subroutines |
| `BREAK` / `CONTINUE` | — | Loop control |
| `RETURN` | — | Exit function early |

### Host synchronisation (requires reading host LED state)

| Command | Purpose |
|---|---|
| `WAIT_FOR_CAPSLOCK_ON` | Block until host turns Caps Lock LED on |
| `WAIT_FOR_CAPSLOCK_OFF` | Block until host turns Caps Lock LED off |
| `WAIT_FOR_NUMLOCK_ON/OFF` | Same for Num Lock |
| `WAIT_FOR_SCROLLLOCK_ON/OFF` | Same for Scroll Lock |
| `SAVE_HOST_KEYBOARD_LOCK_STATE` | Remember current lock state |
| `RESTORE_HOST_KEYBOARD_LOCK_STATE` | Restore saved lock state |

### Mouse commands

| Command | Syntax | Purpose |
|---|---|---|
| `MOUSE_MOVE` | `MOUSE_MOVE 100 -50` | Relative cursor movement (px) |
| `MOUSE_CLICK` | `MOUSE_CLICK LEFT` | Left/Right/Middle click |
| `MOUSE_DOUBLE_CLICK` | `MOUSE_DOUBLE_CLICK LEFT` | Double click |
| `SCROLL_WHEEL` | `SCROLL_WHEEL 3` | Scroll (positive=down) |
| `HOLD` | `HOLD LEFT_BUTTON` | Hold mouse button |
| `RELEASE` | `RELEASE LEFT_BUTTON` | Release held button |

### Key hold / release

| Command | Purpose |
|---|---|
| `HOLD CTRL` | Press Ctrl and keep it held across subsequent commands |
| `RELEASE CTRL` | Release the held Ctrl key |
| `RELEASE_ALL` | Release everything |

Useful for drag operations and complex combos that span multiple lines.

### ATTACKMODE (USB profile switching)

Allows the device to present different USB interface combinations to the host:

| Mode | What it presents |
|---|---|
| `ATTACKMODE HID` | Keyboard + mouse only |
| `ATTACKMODE STORAGE` | USB mass storage (pretend to be a USB drive) |
| `ATTACKMODE HID STORAGE` | Both simultaneously |
| `ATTACKMODE SERIAL` | Serial communication |

### Random commands

| Command | Purpose |
|---|---|
| `RANDOM_DELAY 100 500` | Random pause between min and max ms |
| `RANDOM_LOWERCASE_LETTER` | Type a random a–z character |
| `RANDOM_UPPERCASE_LETTER` | Type a random A–Z character |
| `RANDOM_NUMBER` | Type a random 0–9 digit |
| `RANDOM_SPECIAL_CHAR` | Type a random punctuation/symbol |
| `RANDOM_CHAR` | Any of the above randomly selected |

---

## Current status — our macro engine

### ✅ Already supported

| Command | Notes |
|---|---|
| `STRING text` | Character-by-character via paster |
| `DELAY ms` | Implemented |
| `ENTER TAB ESC BACKSPACE SPACE` | Bare keywords |
| `UP DOWN LEFT RIGHT` | Arrow keys |
| `F1–F12` | Full function key range |
| `HOME END DELETE INSERT PAGEUP PAGEDOWN` | All navigation keys |
| `MOD_KEY CTRL/SHIFT/ALT/GUI + key` | One modifier + one key |
| `# comment` | Line comments |

### 🔜 Easy to add (parser only, no firmware changes, ~1 day)

These only require changes to `src/macro_parser.cpp` and `src/macro_runner.cpp`:

| Command | What's needed |
|---|---|
| `REM comment` | Add `REM` as alias for `#` comment type |
| `STRINGLN text` | Parse as STRING + push synthetic ENTER command |
| `DEFAULTDELAY ms` | Store in parser state; inject after each command during run |
| `REPEAT N` | Track last command in runner; emit N copies |
| `CAPSLOCK / NUMLOCK / SCROLLLOCK` | Add keycodes: 0x39/0x53/0x47 to `keycode_for()` |
| `PRINTSCREEN` | Keycode 0x46 |
| `PAUSE` | Keycode 0x48 |
| `MENU` / `APP` | Keycode 0x65 |
| `RANDOM_DELAY min max` | Generate random uint32 in range in runner |
| `MOD_KEY CTRL SHIFT t` | Already works — MOD_KEY handles multi-modifier via chaining |
| `WINDOWS` / `COMMAND` / `OPTION` as aliases | Add to modifier name lookup |

### 🔧 Medium complexity (parser + runner changes, ~2–3 days)

| Feature | What's needed |
|---|---|
| `HOLD key` / `RELEASE key` | Runner needs to maintain a "held keys" bitmask; `send_report()` sends press without release; `RELEASE` sends empty report for that key |
| `MOUSE_MOVE x y` | Add `MOUSE_MOVE` command type to parser + runner; calls `backend_->send_mouse_move(dx, dy)` which already exists |
| `MOUSE_CLICK LEFT/RIGHT/MIDDLE` | New command type; calls `backend_->send_mouse_button()` which already exists |
| `MOUSE_DOUBLE LEFT` | Two clicks with 80ms between |
| `MOUSE_SCROLL n` | Needs `send_mouse_scroll(int8_t)` added to `IHidBackend` interface |
| `RANDOM_CHAR` variants | Add character tables; pick random entry via `esp_random()` |

### 🏗 Harder (significant new subsystem, ~1 week+)

| Feature | What's needed |
|---|---|
| Variables `VAR $x = "v"` | Symbol table in runner; `std::map<std::string, std::string>`; string interpolation in STRING values |
| `IF/ELSE` conditionals | Parser must build AST (not just linear command list); runner needs branch logic |
| `WHILE` / `FOR` loops | AST + loop stack in runner |
| `FUNCTION` definitions | Symbol table for functions; call stack |

### ⛔ Not practical on this hardware

| Feature | Reason |
|---|---|
| `ATTACKMODE STORAGE` | No USB mass storage implementation; ESP32-S3 doesn't expose this easily with TinyUSB in our config |
| `WAIT_FOR_CAPSLOCK_ON` | Requires reading HID LED reports from the host — host sends LED state via HID output report; TinyUSB has `tud_hid_set_report_cb()` which we'd need to implement and expose as a wait primitive |
| Multi-payload switching | No filesystem-backed payload store; our LittleFS has the UI, not a payload library |

---

## Recommended implementation order

### Phase 1 — Quick wins (~1 day, parser only)

```
REM, STRINGLN, DEFAULTDELAY, REPEAT,
CAPSLOCK, NUMLOCK, SCROLLLOCK, PRINTSCREEN, PAUSE, MENU,
RANDOM_DELAY, key name aliases (WINDOWS, COMMAND, OPTION)
```

These make us compatible with the vast majority of real-world DuckyScript 1.0 payloads found in the wild. No firmware API changes needed.

### Phase 2 — Mouse in macros (~1 day)

```
MOUSE_MOVE x y
MOUSE_CLICK LEFT/RIGHT/MIDDLE
MOUSE_DOUBLE LEFT
MOUSE_SCROLL n
```

The hardware already supports it (jiggler uses the same mouse interface). Just wire the parser/runner commands through to the existing `send_mouse_*` methods.

### Phase 3 — HOLD/RELEASE (~1–2 days)

Needed for drag operations and some attack payloads. Requires careful state management in the runner so held keys are released on cancel/error.

### Phase 4 — Variables + REPEAT/IF (weeks, optional)

Only needed for the most complex DuckyScript 3.0 payloads. Most practical use cases work without it. Implement only if there's a clear use case that needs it.

---

## Files to change

| File | Change |
|---|---|
| `src/macro_parser.h/cpp` | New command types, key name lookup table expansion |
| `src/macro_runner.h/cpp` | DEFAULTDELAY state, REPEAT, HOLD bitmask, mouse dispatch |
| `src/hid/ihid_backend.h` | Add `send_mouse_scroll(int8_t)` virtual method |
| `src/hid/usb_hid.h/cpp` | Implement `send_mouse_scroll()` via `mouse_.move(0,0,scroll)` |
| `data/index.html` | Already updated with full syntax reference and templates |
