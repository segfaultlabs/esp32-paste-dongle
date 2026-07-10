#include "typing_engine.h"

#include <cstdlib>
#include <algorithm>
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace typing {

Config preset(Mode mode) {
    Config cfg;
    cfg.mode = mode;
    switch (mode) {
        case Mode::MAX_SPEED:
            cfg.base_delay_ms = 1;
            cfg.jitter_percent = 0;
            cfg.burst_interval = 0;
            cfg.typo_simulation = false;
            break;
        case Mode::FAST_TYPIST:
            cfg.base_delay_ms = 100;          // ~120 WPM
            cfg.jitter_percent = 10;
            cfg.burst_interval = 30;
            cfg.burst_pause_min_ms = 50;
            cfg.burst_pause_max_ms = 150;
            cfg.typo_simulation = false;
            break;
        case Mode::HUMAN:
            cfg.base_delay_ms = 170;          // ~70 WPM
            cfg.jitter_percent = 20;
            cfg.burst_interval = 10;
            cfg.burst_pause_min_ms = 100;
            cfg.burst_pause_max_ms = 400;
            cfg.typo_simulation = false;
            break;
    }
    return cfg;
}

Engine::Engine(Config cfg) : cfg_(std::move(cfg)) {}

void Engine::set_config(const Config& cfg) {
    cfg_ = cfg;
    reset();
}

void Engine::reset() {
    chars_typed_ = 0;
    typo_counter_ = 0;
}

int Engine::now_ms() const {
    if (cfg_.clock_ms) return cfg_.clock_ms();
#ifdef ARDUINO
    return static_cast<int>(millis());
#else
    return 0;
#endif
}

int Engine::wpm() const {
    // WPM = (chars_per_minute / 5). 5 chars ~= 1 word.
    if (cfg_.base_delay_ms <= 0) return 0;
    int cpm = 60000 / cfg_.base_delay_ms;
    return cpm / 5;
}

int Engine::next_delay_ms() {
    ++chars_typed_;

    if (cfg_.mode == Mode::MAX_SPEED) {
        return 1; // As fast as the test harness allows.
    }

    int delay = cfg_.base_delay_ms;

    // Apply per-key jitter.
    if (cfg_.jitter_percent > 0) {
        int half = (delay * cfg_.jitter_percent) / 100;
        int jitter = half == 0 ? 0 : (std::rand() % (2 * half + 1)) - half;
        delay += jitter;
    }
    delay = std::max(delay, 1);

    // Burst pause.
    if (cfg_.burst_interval > 0 && (chars_typed_ % cfg_.burst_interval == 0)) {
        int range = cfg_.burst_pause_max_ms - cfg_.burst_pause_min_ms;
        int pause = range <= 0 ? cfg_.burst_pause_min_ms
                               : cfg_.burst_pause_min_ms + (std::rand() % (range + 1));
        delay += pause;
    }

    return delay;
}

bool Engine::should_typo() {
    if (!cfg_.typo_simulation || cfg_.typo_frequency <= 0) return false;
    ++typo_counter_;
    if (typo_counter_ >= cfg_.typo_frequency) {
        typo_counter_ = 0;
        return true;
    }
    return false;
}

} // namespace typing
