// "Look busy" human simulation: types random word bursts then backspaces them.
// Runs only when the paster and macro runner are idle.
// Clock is injectable so the class is host-testable.
#pragma once

#include "hid/ihid_backend.h"
#include <functional>
#include <string>

namespace sim {

class HumanSim {
public:
    struct Config {
        bool     enabled         = false;
        uint32_t char_delay_ms   = 130;   // ~90 WPM inter-key delay
        uint32_t pause_ms        = 18000; // idle gap between bursts
        uint8_t  words_per_burst = 8;     // words typed before erasing
    };

    explicit HumanSim(hid::IHidBackend* backend,
                      std::function<unsigned long()> clock_ms = nullptr);

    void   set_config(const Config& cfg);
    Config get_config() const { return config_; }

    // Advance one step. Call from loop() only when paster/macro are idle.
    bool tick();

    bool is_enabled() const { return config_.enabled; }

private:
    hid::IHidBackend*              backend_;
    std::function<unsigned long()> clock_ms_;
    Config                         config_;

    enum class Phase { PAUSING, TYPING, ERASING };
    Phase         phase_    = Phase::PAUSING;
    unsigned long next_ms_  = 0;
    std::string   text_;
    size_t        pos_      = 0;

    unsigned long now() const;
    void          start_burst();
    static std::string random_sentence(int n_words);
};

} // namespace sim
