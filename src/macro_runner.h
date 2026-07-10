// Tick-based executor for parsed macro commands.
// Call tick() from loop() every iteration; it executes one command per call
// and honours DELAY commands via a clock function (injectable for host tests).
#pragma once

#include "macro_parser.h"
#include "hid/ihid_backend.h"
#include <functional>
#include <string>
#include <vector>

namespace macro {

class Runner {
public:
    // clock_ms is optional; on device it defaults to millis().
    explicit Runner(hid::IHidBackend* backend,
                    std::function<unsigned long()> clock_ms = nullptr);

    // Load and begin executing a set of parsed commands.
    void start(std::vector<Command> cmds, const std::string& layout = "US");

    // Advance one step. Returns true while still running.
    bool tick();

    void cancel();

    bool is_running()     const { return running_; }
    int  commands_total() const { return static_cast<int>(cmds_.size()); }
    int  commands_done()  const { return static_cast<int>(idx_); }

private:
    hid::IHidBackend*              backend_;
    std::function<unsigned long()> clock_ms_;
    std::vector<Command>           cmds_;
    std::string                    layout_;
    size_t                         idx_         = 0;
    unsigned long                  delay_until_ = 0;
    bool                           in_delay_    = false;
    bool                           running_     = false;

    unsigned long now_ms() const;
    void exec(const Command& cmd);
};

} // namespace macro
