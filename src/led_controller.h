// Onboard NeoPixel (WS2812B) status indicator.
// Uses the ESP32 core's built-in neopixelWrite() — no extra library.
//
// Default pin: 48 (ESP32-S3-DevKitM-1 / DevKitC-1 onboard RGB).
// Override at build time with -D NEOPIXEL_PIN=<gpio>.
// Set to -1 to disable the LED entirely (classic ESP32, custom boards).
#pragma once

#include <cstdint>

// Boards without an onboard RGB can override this to -1.
#if !defined(NEOPIXEL_PIN)
#  if defined(HID_BACKEND_USB)
#    define NEOPIXEL_PIN 48
#  else
#    define NEOPIXEL_PIN -1
#  endif
#endif

namespace led {

enum class State {
    BOOTING,     // slow white breath — startup
    NO_HOST,     // pulsing cyan    — USB not yet enumerated by host
    IDLE,        // solid dim mint  — ready
    TYPING,      // fast mint pulse — paster active
    JIGGLING,    // soft blue pulse — jiggler active (not typing)
    MACRO,       // orange pulse    — macro running
    SIMULATING,  // purple pulse    — human-sim active
    ERROR        // solid red       — backend failed
};

class Controller {
public:
    void begin(int pin = NEOPIXEL_PIN);
    void set_state(State s);
    void tick();  // call every loop() iteration

private:
    int   pin_   = NEOPIXEL_PIN;
    State state_ = State::BOOTING;
    uint8_t phase_ = 0;
    unsigned long last_tick_ = 0;

    void write(uint8_t r, uint8_t g, uint8_t b);
    uint8_t triangle(); // triangle wave on phase_ → 0-255
};

} // namespace led
