#include "mouse_jiggler.h"

#include <Arduino.h>
#include <cmath>
#include <esp_random.h>

namespace jiggler {

Pattern pattern_from_string(const std::string& s) {
    if (s == "natural") return Pattern::NATURAL;
    if (s == "line")    return Pattern::LINE;
    if (s == "square")  return Pattern::SQUARE;
    if (s == "circle")  return Pattern::CIRCLE;
    return Pattern::RANDOM;
}

const char* pattern_to_string(Pattern p) {
    switch (p) {
        case Pattern::NATURAL: return "natural";
        case Pattern::LINE:    return "line";
        case Pattern::SQUARE:  return "square";
        case Pattern::CIRCLE:  return "circle";
        default:               return "random";
    }
}

Jiggler::Jiggler(hid::IHidBackend* backend) : backend_(backend) {
    current_interval_ms_ = settings_.interval_ms;
}

void Jiggler::set_settings(const Settings& s) {
    settings_ = s;
    current_interval_ms_ = compute_interval();
    step_ = 0;
    // Reset OU state when settings change so the cursor drifts from a clean origin.
    ou_x_ = 0.0f; ou_y_ = 0.0f;
    ou_ix_ = 0;   ou_iy_ = 0;
}

int8_t Jiggler::random_delta() {
    int d = settings_.distance;
    if (d <= 0) d = 1;
    // Random value in [-d, d], excluding 0.
    int v = random(-d, d + 1);
    if (v == 0) v = d;
    return static_cast<int8_t>(v);
}

float Jiggler::gaussian() {
    // Box-Muller transform: two uniform [0,1) values → standard normal.
    float u1 = static_cast<float>((esp_random() >> 1) + 1) / 2147483648.0f;
    float u2 = static_cast<float>(esp_random() >> 1)       / 2147483648.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

uint32_t Jiggler::draw_poisson_interval() {
    // Exponential distribution: interval = -mean * ln(U), U ~ Uniform(0,1).
    // This gives a true Poisson process with the configured mean.
    uint32_t mean = settings_.interval_ms;
    if (mean < 1000) mean = 1000;
    float u = static_cast<float>((esp_random() >> 1) + 1) / 2147483648.0f;
    float interval = -(float)mean * logf(u);
    // Cap at 5× mean so we never wait absurdly long on the tail.
    float cap = (float)mean * 5.0f;
    if (interval > cap)   interval = cap;
    if (interval < 500.0f) interval = 500.0f;
    return static_cast<uint32_t>(interval);
}

uint32_t Jiggler::compute_interval() {
    if (settings_.pattern == Pattern::NATURAL) {
        return draw_poisson_interval();
    }
    uint32_t iv = settings_.interval_ms;
    if (iv < 1000) iv = 1000;
    if (!settings_.randomize_interval) return iv;
    // ±25% jitter for geometric patterns.
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

void Jiggler::do_natural_jiggle() {
    // Ornstein-Uhlenbeck process: dx = -θ·x·dt + σ·dW
    // θ controls mean reversion (pull back to home), σ controls noise magnitude.
    const float theta = 0.18f;  // mean reversion — feels natural, not too sticky
    float max_r = static_cast<float>(settings_.ou_max_radius);
    if (max_r < 1.0f) max_r = 1.0f;
    float sigma = (settings_.ou_jitter / 100.0f) * max_r * 0.55f;

    ou_x_ = ou_x_ * (1.0f - theta) + sigma * gaussian();
    ou_y_ = ou_y_ * (1.0f - theta) + sigma * gaussian();

    // Soft-clamp: if outside max_radius, scale back to the boundary.
    float r = sqrtf(ou_x_ * ou_x_ + ou_y_ * ou_y_);
    if (r > max_r) {
        ou_x_ = ou_x_ * max_r / r;
        ou_y_ = ou_y_ * max_r / r;
    }

    // Quantise to integer pixels, report only the delta from last report.
    int new_ix = static_cast<int>(roundf(ou_x_));
    int new_iy = static_cast<int>(roundf(ou_y_));
    int8_t dx = static_cast<int8_t>(new_ix - ou_ix_);
    int8_t dy = static_cast<int8_t>(new_iy - ou_iy_);
    ou_ix_ = new_ix;
    ou_iy_ = new_iy;

    if (dx != 0 || dy != 0) {
        backend_->send_mouse_move(dx, dy);
    }
}

void Jiggler::do_jiggle() {
    if (settings_.pattern == Pattern::NATURAL) {
        do_natural_jiggle();
        last_jiggle_ms_ = millis();
        current_interval_ms_ = draw_poisson_interval();
        return;
    }

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
