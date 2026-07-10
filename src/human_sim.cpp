#include "human_sim.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <cstdlib>

namespace sim {

// Small embedded wordlist — diverse enough to look like natural typing.
static const char* WORDS[] = {
    "the","quick","brown","fox","jumped","over","lazy","dog",
    "this","system","works","and","types","text","automatically",
    "please","wait","while","processing","your","request","testing",
    "hello","world","checking","functionality","configuration","file",
    "some","random","words","appear","to","simulate","real","typing",
    "lorem","ipsum","dolor","sit","amet","consectetur","adipiscing",
    "connected","ready","active","running","network","device","input",
    "document","server","keyboard","monitor","output","user","data",
};
static const int WORD_COUNT = static_cast<int>(sizeof(WORDS) / sizeof(WORDS[0]));

HumanSim::HumanSim(hid::IHidBackend* backend,
                   std::function<unsigned long()> clock_ms)
    : backend_(backend), clock_ms_(std::move(clock_ms)) {}

unsigned long HumanSim::now() const {
    if (clock_ms_) return clock_ms_();
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

void HumanSim::set_config(const Config& cfg) {
    bool was_enabled = config_.enabled;
    config_ = cfg;
    if (!cfg.enabled && was_enabled) {
        // Turned off mid-burst — reset state cleanly.
        phase_ = Phase::PAUSING;
        text_.clear();
        pos_ = 0;
        next_ms_ = 0;
    }
}

std::string HumanSim::random_sentence(int n_words) {
    std::string s;
    for (int i = 0; i < n_words; ++i) {
        if (i) s += ' ';
        s += WORDS[std::rand() % WORD_COUNT];
    }
    return s;
}

void HumanSim::start_burst() {
    text_    = random_sentence(config_.words_per_burst);
    pos_     = 0;
    phase_   = Phase::TYPING;
    next_ms_ = now();
}

bool HumanSim::tick() {
    if (!config_.enabled || !backend_) return false;

    unsigned long t = now();

    switch (phase_) {
        case Phase::PAUSING:
            if (t >= next_ms_) start_burst();
            break;

        case Phase::TYPING:
            if (t >= next_ms_) {
                if (pos_ < text_.size()) {
                    backend_->send_char(text_[pos_++]);
                    next_ms_ = t + config_.char_delay_ms;
                } else {
                    phase_   = Phase::ERASING;
                    next_ms_ = t + config_.char_delay_ms;
                }
            }
            break;

        case Phase::ERASING:
            if (t >= next_ms_) {
                if (pos_ > 0) {
                    backend_->send_key(0x2A); // BACKSPACE
                    --pos_;
                    // Erase at ~1.3× typing speed so it feels human.
                    next_ms_ = t + (config_.char_delay_ms * 3 / 4);
                } else {
                    phase_   = Phase::PAUSING;
                    next_ms_ = t + config_.pause_ms;
                }
            }
            break;
    }
    return true;
}

} // namespace sim
