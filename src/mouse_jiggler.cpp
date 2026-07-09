#include "mouse_jiggler.h"

#include <Arduino.h>
#include <cmath>

namespace jiggler {

Pattern pattern_from_string(const std::string& s) {
    if (s == "line") return Pattern::LINE;
    if (s == "square") return Pattern::SQUARE;
    if (s == "circle") return Pattern::CIRCLE;
    return Pattern::RANDOM;
}

const char* pattern_to_string(Pattern p) {
    switch (p) {
        case Pattern::LINE: return "line";
        case Pattern::SQUARE: return "square";
        case Pattern::CIRCLE: return "circle";
        default: return "random";
    }
}

Jiggler::Jiggler(hid::IHidBackend* backend) : backend_(backend) {
    current_interval_ms_ = settings_.interval_ms;
}

void Jiggler::set_settings(const Settings& s) {
    settings_ = s;
    current_interval_ms_ = compute_interval();
    step_ = 0;
}

int8_t Jiggler::random_delta() {
    int d = settings_.distance;
    if (d <= 0) d = 1;
    // Random value in [-d, d], excluding 0.
    int v = random(-d, d + 1);
    if (v == 0) v = d;
    return static_cast<int8_t>(v);
}

uint32_t Jiggler::compute_interval() {
    uint32_t iv = settings_.interval_ms;
    if (iv < 1000) iv = 1000; // sanity: no faster than 1 Hz
    if (!settings_.randomize_interval) return iv;
    // +/- 25% jitter.
    int32_t jitter = random(-static_cast<int32_t>(iv) / 4, static_cast<int32_t>(iv) / 4 + 1);
    int64_t result = static_cast<int64_t>(iv) + jitter;
    if (result < 1000) result = 1000;
    return static_cast<uint32_t>(result);
}

bool Jiggler::should_jiggle(unsigned long now) {
    if (!settings_.enabled) return false;
    if (!backend_ || !backend_->has_mouse() || !backend_->is_connected()) return false;
    if (now - last_jiggle_ms_ >= current_interval_ms_) return true;
    return false;
}

void Jiggler::do_jiggle() {
    int8_t dx = 0;
    int8_t dy = 0;

    switch (settings_.pattern) {
        case Pattern::LINE: {
            // Move right, then back left.
            dx = (step_ % 2 == 0) ? settings_.distance : -settings_.distance;
            break;
        }
        case Pattern::SQUARE: {
            // Four-step square: right, down, left, up.
            switch (step_ % 4) {
                case 0: dx = settings_.distance; break;
                case 1: dy = settings_.distance; break;
                case 2: dx = -settings_.distance; break;
                case 3: dy = -settings_.distance; break;
            }
            break;
        }
        case Pattern::CIRCLE: {
            // Eight-step approximate circle.
            const float steps = 8.0f;
            float angle = static_cast<float>(step_ % 8) * (2.0f * PI / steps);
            dx = static_cast<int8_t>(std::round(std::cos(angle) * settings_.distance));
            dy = static_cast<int8_t>(std::round(std::sin(angle) * settings_.distance));
            break;
        }
        case Pattern::RANDOM:
        default: {
            dx = random_delta();
            dy = random_delta();
            break;
        }
    }

    if (dx != 0 || dy != 0) {
        backend_->send_mouse_move(dx, dy);
    }

    ++step_;
    last_jiggle_ms_ = millis();
    current_interval_ms_ = compute_interval();
}

void Jiggler::tick() {
    unsigned long now = millis();
    if (should_jiggle(now)) {
        do_jiggle();
    }
}

} // namespace jiggler
