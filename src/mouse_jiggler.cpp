#include "mouse_jiggler.h"

#include <Arduino.h>
#include <algorithm>
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
    // Ornstein-Uhlenbeck process: compute the NEXT target position.
    const float theta = 0.18f;
    float max_r = static_cast<float>(settings_.ou_max_radius);
    if (max_r < 1.0f) max_r = 1.0f;
    float sigma = (settings_.ou_jitter / 100.0f) * max_r * 0.55f;

    float new_x = ou_x_ * (1.0f - theta) + sigma * gaussian();
    float new_y = ou_y_ * (1.0f - theta) + sigma * gaussian();

    float r = sqrtf(new_x * new_x + new_y * new_y);
    if (r > max_r) { new_x = new_x * max_r / r; new_y = new_y * max_r / r; }

    // Instead of jumping to the target in one frame, set up a smooth animation
    // that sends many small HID reports over ou_anim_ms milliseconds.
    // loop() runs every ~5ms, so ticks ≈ ou_anim_ms / 5.
    uint16_t anim_ms = settings_.ou_anim_ms;
    if (anim_ms < 50)  anim_ms = 50;
    if (anim_ms > 800) anim_ms = 800;
    int steps = static_cast<int>(anim_ms / 5);
    if (steps < 1) steps = 1;

    float total_dx = new_x - ou_x_;
    float total_dy = new_y - ou_y_;

    // Update the continuous OU position to the new target.
    ou_x_ = new_x;
    ou_y_ = new_y;

    // Set animation: spread the total pixel delta over 'steps' ticks.
    anim_dx_per_tick_ = total_dx / static_cast<float>(steps);
    anim_dy_per_tick_ = total_dy / static_cast<float>(steps);
    anim_acc_x_       = 0.0f;
    anim_acc_y_       = 0.0f;
    anim_steps_left_  = steps;
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

    // Drive smooth animation: send one sub-step toward the current OU target.
    if (anim_steps_left_ > 0 && backend_ && backend_->has_mouse() && backend_->is_connected()) {
        anim_acc_x_ += anim_dx_per_tick_;
        anim_acc_y_ += anim_dy_per_tick_;

        // Extract whole-pixel deltas from the sub-pixel accumulator.
        int ix = static_cast<int>(anim_acc_x_);
        int iy = static_cast<int>(anim_acc_y_);
        if (ix != 0 || iy != 0) {
            int8_t dx = static_cast<int8_t>(std::max(-127, std::min(127, ix)));
            int8_t dy = static_cast<int8_t>(std::max(-127, std::min(127, iy)));
            backend_->send_mouse_move(dx, dy);
            anim_acc_x_ -= static_cast<float>(ix);
            anim_acc_y_ -= static_cast<float>(iy);
            ou_ix_ += dx;
            ou_iy_ += dy;
        }
        --anim_steps_left_;
    }

    // Fire a new OU event when the Poisson interval elapses.
    if (should_jiggle(now)) {
        do_jiggle();
    }
}

} // namespace jiggler
