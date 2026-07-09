// Keystroke timing / jitter engine.
// Host-testable: inject a clock function to make delays deterministic in tests.
#pragma once

#include <cstdint>
#include <functional>

namespace typing {

enum class Mode {
    MAX_SPEED,   // No jitter, minimal delay
    FAST_TYPIST, // ~100-120 WPM, light jitter
    HUMAN        // ~60-80 WPM, heavy jitter, pauses, optional typos
};

struct Config {
    Mode mode = Mode::FAST_TYPIST;

    // Base inter-key delay in milliseconds.
    int base_delay_ms = 50;

    // Jitter range as +/- percentage of base delay.
    int jitter_percent = 10;

    // Pause inserted after every N characters.
    int burst_interval = 20;
    int burst_pause_min_ms = 50;
    int burst_pause_max_ms = 300;

    // Optional typo simulation.
    bool typo_simulation = false;
    int typo_frequency = 100; // One typo per N characters on average.

    // Current injected clock (milliseconds). Defaults to real time.
    std::function<int()> clock_ms = nullptr;
};

// Sensible defaults for each speed mode.
Config preset(Mode mode);

class Engine {
public:
    explicit Engine(Config cfg);

    // Set or update configuration at runtime.
    void set_config(const Config& cfg);

    // Approximate WPM for the current config.
    int wpm() const;

    // Time in ms to wait before the next keystroke.
    int next_delay_ms();

    // True if the next character should be a simulated typo.
    bool should_typo();

    // Reset internal state (counters, RNG seed).
    void reset();

private:
    Config cfg_;
    int chars_typed_ = 0;
    int typo_counter_ = 0;
    int now_ms() const;
};

} // namespace typing
