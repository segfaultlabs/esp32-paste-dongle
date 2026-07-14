// Software mouse jiggler: moves the host cursor periodically to prevent sleep.
#pragma once

#include <cstdint>
#include <string>
#include "hid/ihid_backend.h"

namespace jiggler {

enum class Pattern {
    NATURAL, // Poisson intervals + Ornstein-Uhlenbeck drift (recommended, undetectable)
    RANDOM,  // small random vector every interval
    LINE,    // move right then left
    SQUARE,  // small square
    CIRCLE   // small circle
};

Pattern pattern_from_string(const std::string& s);
const char* pattern_to_string(Pattern p);

struct Settings {
    bool enabled = false;
    uint32_t interval_ms = 30000;       // mean interval (Poisson) or fixed interval
    int8_t distance = 2;                // pixels per step (geometric patterns only)
    Pattern pattern = Pattern::NATURAL; // default to natural mode
    bool randomize_interval = false;    // ±25% jitter (geometric patterns only)
    uint8_t  ou_max_radius = 5;          // natural: max pixels from home (1-20)
    uint8_t  ou_jitter     = 50;         // natural: drift strength 0-100%
    uint16_t ou_anim_ms    = 300;        // natural: how long each drift movement takes (50-800ms)
};

class Jiggler {
public:
    explicit Jiggler(hid::IHidBackend* backend);

    void set_settings(const Settings& s);
    Settings get_settings() const { return settings_; }

    // Call from loop() as often as possible.
    void tick();

    // True if the jiggler is currently enabled.
    bool is_enabled() const { return settings_.enabled; }

private:
    hid::IHidBackend* backend_;
    Settings settings_;

    unsigned long last_jiggle_ms_ = 0;
    uint32_t current_interval_ms_ = 30000;
    int step_ = 0;

    // Ornstein-Uhlenbeck state (natural mode)
    float ou_x_ = 0.0f, ou_y_ = 0.0f;   // continuous position (pixels from home)
    int   ou_ix_ = 0,   ou_iy_ = 0;     // last integer position reported to HID

    // Smooth animation state — instead of jumping to the new OU position in one
    // frame, we glide there over ou_anim_ms by sending many small HID reports.
    float anim_dx_per_tick_ = 0.0f;
    float anim_dy_per_tick_ = 0.0f;
    float anim_acc_x_       = 0.0f;  // sub-pixel accumulator for animation steps
    float anim_acc_y_       = 0.0f;
    int   anim_steps_left_  = 0;

    bool     should_jiggle(unsigned long now);
    void     do_jiggle();
    void     do_natural_jiggle();
    int8_t   random_delta();
    uint32_t compute_interval();
    uint32_t draw_poisson_interval();
    static float gaussian(); // standard normal via Box-Muller
};

} // namespace jiggler
