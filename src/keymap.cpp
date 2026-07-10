#include "keymap.h"

#include <cctype>
#include <unordered_map>
#include <vector>

namespace keymap {

// HID usage IDs for standard US ASCII characters.
// Based on USB HID Usage Tables, Keyboard/Keypad Page (0x07).
static const std::unordered_map<char, Report> us_map = {
    // Lowercase letters
    {'a', Report(0x04, 0, true)}, {'b', Report(0x05, 0, true)}, {'c', Report(0x06, 0, true)},
    {'d', Report(0x07, 0, true)}, {'e', Report(0x08, 0, true)}, {'f', Report(0x09, 0, true)},
    {'g', Report(0x0A, 0, true)}, {'h', Report(0x0B, 0, true)}, {'i', Report(0x0C, 0, true)},
    {'j', Report(0x0D, 0, true)}, {'k', Report(0x0E, 0, true)}, {'l', Report(0x0F, 0, true)},
    {'m', Report(0x10, 0, true)}, {'n', Report(0x11, 0, true)}, {'o', Report(0x12, 0, true)},
    {'p', Report(0x13, 0, true)}, {'q', Report(0x14, 0, true)}, {'r', Report(0x15, 0, true)},
    {'s', Report(0x16, 0, true)}, {'t', Report(0x17, 0, true)}, {'u', Report(0x18, 0, true)},
    {'v', Report(0x19, 0, true)}, {'w', Report(0x1A, 0, true)}, {'x', Report(0x1B, 0, true)},
    {'y', Report(0x1C, 0, true)}, {'z', Report(0x1D, 0, true)},

    // Digits
    {'1', Report(0x1E, 0, true)}, {'2', Report(0x1F, 0, true)}, {'3', Report(0x20, 0, true)},
    {'4', Report(0x21, 0, true)}, {'5', Report(0x22, 0, true)}, {'6', Report(0x23, 0, true)},
    {'7', Report(0x24, 0, true)}, {'8', Report(0x25, 0, true)}, {'9', Report(0x26, 0, true)},
    {'0', Report(0x27, 0, true)},

    // Common symbols (US layout)
    {'\n', Report(0x28, 0, true)}, // ENTER
    {'\t', Report(0x2B, 0, true)}, // TAB
    {' ',  Report(0x2C, 0, true)}, // SPACE
    {'-',  Report(0x2D, 0, true)},
    {'=',  Report(0x2E, 0, true)},
    {'[',  Report(0x2F, 0, true)},
    {']',  Report(0x30, 0, true)},
    {'\\', Report(0x31, 0, true)},
    {';',  Report(0x33, 0, true)},
    {'\'', Report(0x34, 0, true)},
    {'`',  Report(0x35, 0, true)},
    {',',  Report(0x36, 0, true)},
    {'.',  Report(0x37, 0, true)},
    {'/',  Report(0x38, 0, true)},

    // Shifted symbols
    {'!', Report(0x1E, MOD_SHIFT, true)},
    {'@', Report(0x1F, MOD_SHIFT, true)},
    {'#', Report(0x20, MOD_SHIFT, true)},
    {'$', Report(0x21, MOD_SHIFT, true)},
    {'%', Report(0x22, MOD_SHIFT, true)},
    {'^', Report(0x23, MOD_SHIFT, true)},
    {'&', Report(0x24, MOD_SHIFT, true)},
    {'*', Report(0x25, MOD_SHIFT, true)},
    {'(', Report(0x26, MOD_SHIFT, true)},
    {')', Report(0x27, MOD_SHIFT, true)},
    {'_', Report(0x2D, MOD_SHIFT, true)},
    {'+', Report(0x2E, MOD_SHIFT, true)},
    {'{', Report(0x2F, MOD_SHIFT, true)},
    {'}', Report(0x30, MOD_SHIFT, true)},
    {'|', Report(0x31, MOD_SHIFT, true)},
    {':', Report(0x33, MOD_SHIFT, true)},
    {'"', Report(0x34, MOD_SHIFT, true)},
    {'~', Report(0x35, MOD_SHIFT, true)},
    {'<', Report(0x36, MOD_SHIFT, true)},
    {'>', Report(0x37, MOD_SHIFT, true)},
    {'?', Report(0x38, MOD_SHIFT, true)},
};

// Dvorak layout: map the character we want to appear on a Dvorak host
// to the HID report (QWERTY scancode + modifiers) that produces it.
static const std::unordered_map<char, Report> dvorak_map = {
    // Lowercase letters. Each entry maps the desired Dvorak character to the
    // QWERTY scancode that produces it on a Dvorak host.
    {'a', Report(0x04, 0, true)}, {'b', Report(0x11, 0, true)}, {'c', Report(0x0C, 0, true)},
    {'d', Report(0x0B, 0, true)}, {'e', Report(0x07, 0, true)}, {'f', Report(0x1C, 0, true)},
    {'g', Report(0x18, 0, true)}, {'h', Report(0x0D, 0, true)}, {'i', Report(0x0A, 0, true)},
    {'j', Report(0x06, 0, true)}, {'k', Report(0x19, 0, true)}, {'l', Report(0x13, 0, true)},
    {'m', Report(0x10, 0, true)}, {'n', Report(0x0F, 0, true)}, {'o', Report(0x16, 0, true)},
    {'p', Report(0x15, 0, true)}, {'q', Report(0x1B, 0, true)}, {'r', Report(0x12, 0, true)},
    {'s', Report(0x33, 0, true)}, {'t', Report(0x0E, 0, true)}, {'u', Report(0x09, 0, true)},
    {'v', Report(0x37, 0, true)}, {'w', Report(0x36, 0, true)}, {'x', Report(0x05, 0, true)},
    {'y', Report(0x17, 0, true)}, {'z', Report(0x38, 0, true)},

    // Digits (same physical keys as US)
    {'1', Report(0x1E, 0, true)}, {'2', Report(0x1F, 0, true)}, {'3', Report(0x20, 0, true)},
    {'4', Report(0x21, 0, true)}, {'5', Report(0x22, 0, true)}, {'6', Report(0x23, 0, true)},
    {'7', Report(0x24, 0, true)}, {'8', Report(0x25, 0, true)}, {'9', Report(0x26, 0, true)},
    {'0', Report(0x27, 0, true)},

    // Whitespace / controls
    {'\n', Report(0x28, 0, true)},
    {'\t', Report(0x2B, 0, true)},
    {' ',  Report(0x2C, 0, true)},

    // Dvorak symbols
    {'[',  Report(0x2D, 0, true)},  // QWERTY -
    {']',  Report(0x2E, 0, true)},  // QWERTY =
    {'/',  Report(0x2F, 0, true)},  // QWERTY [
    {'=',  Report(0x30, 0, true)},  // QWERTY ]
    {'\\', Report(0x31, 0, true)},  // QWERTY \
    {'-',  Report(0x34, 0, true)},  // QWERTY '
    {';',  Report(0x1D, 0, true)},  // QWERTY z
    {'\'', Report(0x14, 0, true)},  // QWERTY q
    {',',  Report(0x1A, 0, true)},  // QWERTY w
    {'.',  Report(0x08, 0, true)},  // QWERTY e
    {'`',  Report(0x35, 0, true)},  // QWERTY `

    // Shifted symbols
    {'!', Report(0x1E, MOD_SHIFT, true)},
    {'@', Report(0x1F, MOD_SHIFT, true)},
    {'#', Report(0x20, MOD_SHIFT, true)},
    {'$', Report(0x21, MOD_SHIFT, true)},
    {'%', Report(0x22, MOD_SHIFT, true)},
    {'^', Report(0x23, MOD_SHIFT, true)},
    {'&', Report(0x24, MOD_SHIFT, true)},
    {'*', Report(0x25, MOD_SHIFT, true)},
    {'(', Report(0x26, MOD_SHIFT, true)},
    {')', Report(0x27, MOD_SHIFT, true)},
    {'{', Report(0x2D, MOD_SHIFT, true)}, // QWERTY - + shift
    {'}', Report(0x2E, MOD_SHIFT, true)}, // QWERTY = + shift
    {'?', Report(0x2F, MOD_SHIFT, true)}, // QWERTY [ + shift
    {'+', Report(0x30, MOD_SHIFT, true)}, // QWERTY ] + shift
    {'|', Report(0x31, MOD_SHIFT, true)}, // QWERTY \ + shift
    {'_', Report(0x34, MOD_SHIFT, true)}, // QWERTY ' + shift
    {':', Report(0x1D, MOD_SHIFT, true)}, // QWERTY z + shift
    {'"', Report(0x14, MOD_SHIFT, true)}, // QWERTY q + shift
    {'<', Report(0x1A, MOD_SHIFT, true)}, // QWERTY w + shift
    {'>', Report(0x08, MOD_SHIFT, true)}, // QWERTY e + shift
    {'~', Report(0x35, MOD_SHIFT, true)}, // QWERTY ` + shift
};

// UK ISO layout.  Start from the US map and override the characters
// that differ when the host OS is set to UK QWERTY.
//
// Key differences (UK host perspective):
//   Shift+2        → "  (US: @)
//   Shift+3        → £  (US: #, but £ is non-ASCII so omitted here)
//   Shift+'        → @  (US: ")
//   Non-US # key   → #  (scancode 0x32, the key between ' and Enter on UK)
//   Non-US # + SHF → ~
//   ISO extra key  → \  (scancode 0x64, between left-shift and Z on UK ISO)
//   ISO extra + SHF→ |
static std::unordered_map<char, Report> build_uk_map() {
    auto m = us_map; // inherit everything from US

    // Swap @/" which are on different keys in UK
    m['@']  = Report(0x34, MOD_SHIFT, true);  // shift + apostrophe key
    m['"']  = Report(0x1F, MOD_SHIFT, true);  // shift + 2 key

    // # and ~ live on the non-US hash key (0x32) between ' and Enter
    m['#']  = Report(0x32, 0,         true);
    m['~']  = Report(0x32, MOD_SHIFT, true);

    // \ and | are on the ISO extra key (0x64) between left-shift and Z
    m['\\'] = Report(0x64, 0,         true);
    m['|']  = Report(0x64, MOD_SHIFT, true);

    return m;
}
static const std::unordered_map<char, Report> uk_map = build_uk_map();

// ---- per-layout helpers ------------------------------------------------

static const std::unordered_map<char, Report>& map_for(const std::string& layout) {
    if (layout == "UK")     return uk_map;
    if (layout == "DVORAK") return dvorak_map;
    return us_map;
}

static Report dvorak_lookup_char(char ch) {
    auto it = dvorak_map.find(ch);
    if (it != dvorak_map.end()) return it->second;
    return {0, 0, false};
}

Report lookup(char ch, const std::string& layout) {
    if (!is_supported(layout)) return {0, 0, false};

    if (layout == "DVORAK") {
        Report r = dvorak_lookup_char(ch);
        if (r.valid) return r;
        if (ch >= 'A' && ch <= 'Z') {
            Report lower = dvorak_lookup_char(static_cast<char>(ch - 'A' + 'a'));
            if (lower.valid) { lower.modifiers |= MOD_SHIFT; return lower; }
        }
        return {0, 0, false};
    }

    // US and UK share the same uppercase fallback logic.
    const auto& m = map_for(layout);
    auto it = m.find(ch);
    if (it != m.end()) return it->second;

    if (ch >= 'A' && ch <= 'Z') {
        auto lower = m.find(static_cast<char>(ch - 'A' + 'a'));
        if (lower != m.end()) {
            Report r = lower->second;
            r.modifiers |= MOD_SHIFT;
            return r;
        }
    }
    return {0, 0, false};
}

std::vector<std::string> layouts() {
    return {"US", "UK", "DVORAK"};
}

bool is_supported(const std::string& layout) {
    return layout == "US" || layout == "UK" || layout == "DVORAK";
}

std::string supported_chars(const std::string& layout) {
    const auto& m = map_for(layout);
    std::string out;
    out.reserve(m.size() * 2);
    for (const auto& kv : m) {
        out += kv.first;
    }
    // Also add uppercase variants that are derived at lookup time.
    for (char c = 'A'; c <= 'Z'; ++c) {
        out += c;
    }
    return out;
}

} // namespace keymap
