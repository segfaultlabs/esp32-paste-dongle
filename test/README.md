# Host-Side Unit Tests

These tests exercise the hardware-independent logic on your development machine before flashing to the ESP32.

## What Is Tested

- **Macro parser** — DuckScript-like `STRING`, `DELAY`, `ENTER`, `TAB`, `CTRL+A`, comments.
- **Keymap / layout** — character-to-HID-keycode mapping for US layout.
- **Typing engine** — speed modes, WPM calculation, jitter range, burst pauses, typo simulation.

## Requirements

- A C++17 compiler (`g++` or `clang++`).
- `make`.

## Build & Run

```bash
cd test
make run
```

To build without running:

```bash
make
```

To clean:

```bash
make clean
```

## Adding Tests

1. Create `test_your_feature.cpp`.
2. Include `test_harness.h` and the relevant `../src/*.h` header.
3. Add the test binary rule to the `Makefile`.
4. Add it to the `TESTS` list and `run` target.

## Note

These tests use stub implementations in `src/`. As the firmware grows, the same source files will be compiled for both the host tests and the ESP32 target, ensuring the logic behaves identically.
