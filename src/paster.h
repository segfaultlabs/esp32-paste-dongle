// High-level paste session manager.
// Feeds text to an HID backend one character at a time with configurable
// speed / jitter so the host sees human-like typing.
#pragma once

#include <functional>
#include <string>

#include "hid/ihid_backend.h"
#include "typing_engine.h"

namespace paster {

enum class State {
    IDLE,
    TYPING,
    DONE,
    CANCELLED,
    ERROR,
};

struct Status {
    State state = State::IDLE;
    int chars_typed = 0;
    int total_chars = -1;  // -1 if the client never told us the total.
    int pending = 0;       // Characters queued but not yet typed.
    int wpm = 0;
};

class Paster {
public:
    // `clock_ms` is optional; on device it defaults to Arduino millis().
    Paster(hid::IHidBackend* backend,
           const typing::Config& cfg,
           std::function<int()> clock_ms = nullptr);

    void set_config(const typing::Config& cfg);

    // Start a new paste. total_chars = -1 means "unknown / streaming".
    void start(int total_chars = -1);

    // Queue more text to be typed. Safe to call at any time.
    void feed(const std::string& chunk);

    // Stop typing and discard queued text.
    void cancel();

    // Call frequently from the main loop. Returns true while still active.
    bool tick();

    Status status() const;

private:
    hid::IHidBackend* backend_;
    typing::Engine engine_;
    std::function<int()> clock_ms_;

    std::string buffer_;
    size_t read_pos_ = 0;

    int chars_typed_ = 0;
    int total_chars_ = -1;
    State state_ = State::IDLE;
    int next_time_ms_ = 0;

    int now_ms() const;
    void send_one();
    int pending() const { return static_cast<int>(buffer_.size() - read_pos_); }
};

} // namespace paster
