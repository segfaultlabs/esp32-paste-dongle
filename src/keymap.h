// Keyboard layout mapping from characters to HID keycodes + modifiers.
// Host-testable with no hardware dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace keymap {

struct Report {
    uint8_t keycode = 0;
    uint8_t modifiers = 0;
    bool valid = false;

    Report() = default;
    Report(uint8_t k, uint8_t m, bool v) : keycode(k), modifiers(m), valid(v) {}
};

// Look up the HID report for a character in a given layout.
// Supported layouts: "US", "DVORAK".
Report lookup(char ch, const std::string& layout = "US");

// Convenience: SHIFT modifier bit.
constexpr uint8_t MOD_SHIFT = 0x02;

// List supported layout names.
std::vector<std::string> layouts();

// Check whether a layout name is supported.
bool is_supported(const std::string& layout);

} // namespace keymap
