#include "led_controller.h"

#ifdef ARDUINO
#include <Arduino.h>
// neopixelWrite is declared in esp32-hal-rgb-led.h, included via Arduino.h
// on esp32 targets. Forward-declare it here to avoid the full header.
extern "C" void neopixelWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b);
#endif

namespace led {

void Controller::begin(int pin) {
    pin_ = pin;
    write(0, 0, 0);
}

void Controller::set_state(State s) {
    if (s != state_) {
        state_ = s;
        phase_ = 0;
    }
}

uint8_t Controller::triangle() {
    return (phase_ < 128) ? static_cast<uint8_t>(phase_ * 2)
                          : static_cast<uint8_t>((255 - phase_) * 2);
}

void Controller::tick() {
    unsigned long now = 0;
#ifdef ARDUINO
    now = millis();
#endif
    if (now - last_tick_ < 16) return; // ~60 fps cap
    last_tick_ = now;
    ++phase_;

    uint8_t v = triangle();

    switch (state_) {
        case State::BOOTING:
            { uint8_t b = v / 6; write(b, b, b); }      // slow white breath
            break;
        case State::NO_HOST:
            write(0, v / 4, v / 4);                       // pulsing cyan
            break;
        case State::IDLE:
            write(0, 12, 6);                               // solid dim mint (no animation)
            break;
        case State::TYPING: {
            // Fast pulse: square the value so it peaks sharply
            uint16_t vv = static_cast<uint16_t>(v) * v / 255;
            write(0, static_cast<uint8_t>(vv), static_cast<uint8_t>(vv / 2));
            break;
        }
        case State::JIGGLING:
            write(0, 0, v / 4);                            // soft blue pulse
            break;
        case State::MACRO:
            write(v / 4, v / 12, 0);                      // orange pulse
            break;
        case State::SIMULATING:
            write(v / 6, 0, v / 6);                       // purple pulse
            break;
        case State::ERROR:
            write(50, 0, 0);                               // solid red
            break;
    }
}

void Controller::write(uint8_t r, uint8_t g, uint8_t b) {
#ifdef ARDUINO
    if (pin_ < 0) return;
    neopixelWrite(static_cast<uint8_t>(pin_), r, g, b);
#else
    (void)r; (void)g; (void)b;
#endif
}

} // namespace led
