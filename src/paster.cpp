#include "paster.h"

#include <algorithm>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace paster {

Paster::Paster(hid::IHidBackend* backend,
               const typing::Config& cfg,
               std::function<int()> clock_ms)
    : backend_(backend), engine_(cfg), clock_ms_(std::move(clock_ms)) {
}

void Paster::set_config(const typing::Config& cfg) {
    engine_.set_config(cfg);
}

void Paster::start(int total_chars) {
    buffer_.clear();
    read_pos_ = 0;
    chars_typed_ = 0;
    total_chars_ = total_chars;
    state_ = State::TYPING;
    engine_.reset();
    next_time_ms_ = now_ms();
}

void Paster::feed(const std::string& chunk) {
    if (chunk.empty()) return;
    if (state_ == State::DONE || state_ == State::CANCELLED || state_ == State::ERROR) {
        start(total_chars_);
    }
    buffer_ += chunk;
    if (state_ == State::IDLE) {
        state_ = State::TYPING;
        next_time_ms_ = now_ms();
    }
}

void Paster::cancel() {
    buffer_.clear();
    read_pos_ = 0;
    state_ = State::CANCELLED;
}

bool Paster::tick() {
    if (!backend_) {
        state_ = State::ERROR;
        return false;
    }
    if (state_ != State::TYPING) {
        return false;
    }
    if (!backend_->is_connected()) {
        // Wait for the host to connect.
        return true;
    }
    if (pending() == 0) {
        state_ = State::DONE;
        return false;
    }

    int now = now_ms();
    if (now < next_time_ms_) {
        return true;
    }

    send_one();
    int delay = engine_.next_delay_ms();
    next_time_ms_ = now + delay;
    return true;
}

Status Paster::status() const {
    Status s;
    s.state = state_;
    s.chars_typed = chars_typed_;
    s.total_chars = total_chars_;
    s.pending = pending();
    s.wpm = engine_.wpm();
    return s;
}

int Paster::now_ms() const {
    if (clock_ms_) {
        return clock_ms_();
    }
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

void Paster::send_one() {
    if (pending() == 0) return;
    char ch = buffer_[read_pos_++];
    backend_->send_char(ch);
    ++chars_typed_;
    if (read_pos_ >= buffer_.size()) {
        buffer_.clear();
        read_pos_ = 0;
    }
}

} // namespace paster
