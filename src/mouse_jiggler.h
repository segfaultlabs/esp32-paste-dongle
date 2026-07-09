// Software mouse jiggler: moves the host cursor periodically to prevent sleep.
#pragma once

#include <cstdint>
#include <string>
#include "hid/ihid_backend.h"

namespace jiggler {

enum class Pattern {
    RANDOM, // small random vector every interval
    LINE,   // move right then left
    SQUARE, // small square
    CIRCLE  // small circle
};

Pattern pattern_from_string(const std::string& s);
const char* pattern_to_string(Pattern p);

struct Settings {
    bool enabled = false;
    uint32_t interval_ms = 30000; // time between jiggles
    int8_t distance = 2;          // pixels per step
    Pattern pattern = Pattern::RANDOM;
    bool randomize_interval = false; // +/- 25% jitter on interval
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

    bool should_jiggle(unsigned long now);
    void do_jiggle();
    int8_t random_delta();
    uint32_t compute_interval();
};

} // namespace jiggler
