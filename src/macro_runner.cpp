#include "macro_runner.h"
#include "keymap.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cctype>

namespace macro {

Runner::Runner(hid::IHidBackend* backend,
               std::function<unsigned long()> clock_ms)
    : backend_(backend), clock_ms_(std::move(clock_ms)) {}

unsigned long Runner::now_ms() const {
    if (clock_ms_) return clock_ms_();
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

void Runner::start(std::vector<Command> cmds, const std::string& layout) {
    cmds_        = std::move(cmds);
    layout_      = layout;
    idx_         = 0;
    delay_until_ = 0;
    in_delay_    = false;
    running_     = !cmds_.empty();
}

void Runner::cancel() {
    running_  = false;
    in_delay_ = false;
    backend_->release_all();
}

bool Runner::tick() {
    if (!running_) return false;

    if (in_delay_) {
        if (now_ms() < delay_until_) return true;
        in_delay_ = false;
        ++idx_;
    }

    if (idx_ >= cmds_.size()) {
        running_ = false;
        return false;
    }

    exec(cmds_[idx_]);

    // If the command we just ran wasn't a delay, check if we finished.
    if (!in_delay_ && idx_ >= cmds_.size()) {
        running_ = false;
        return false;
    }
    return true;
}

void Runner::exec(const Command& cmd) {
    switch (cmd.type) {
        case CommandType::STRING:
            for (char ch : cmd.text) backend_->send_char(ch);
            ++idx_;
            break;

        case CommandType::DELAY:
            delay_until_ = now_ms() + static_cast<unsigned long>(cmd.delay_ms);
            in_delay_    = true;
            break;

        case CommandType::KEY:
            backend_->send_key(keycode_for(cmd.key));
            ++idx_;
            break;

        case CommandType::MOD_KEY: {
            uint8_t mod = 0;
            if (cmd.modifier == "CTRL")  mod = 0x01;
            if (cmd.modifier == "SHIFT") mod = 0x02;
            if (cmd.modifier == "ALT")   mod = 0x04;
            if (cmd.modifier == "GUI")   mod = 0x08;
            if (!cmd.key.empty()) {
                char lower = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(cmd.key[0])));
                auto r = keymap::lookup(lower, layout_);
                if (r.valid) backend_->send_key(r.keycode, mod);
            }
            ++idx_;
            break;
        }

        case CommandType::COMMENT:
        case CommandType::EMPTY:
        case CommandType::UNKNOWN:
        default:
            ++idx_;
            break;
    }
}

} // namespace macro
