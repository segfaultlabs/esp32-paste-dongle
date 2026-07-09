// Macro / script parser for DuckScript-like snippets.
// Designed to be host-testable with no hardware dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace macro {

enum class CommandType {
    STRING,      // TYPE text literally
    DELAY,       // Wait N milliseconds
    KEY,         // Press a named special key
    MOD_KEY,     // Modifier + key, e.g. CTRL+A
    COMMENT,     // Line is a comment (no-op)
    EMPTY,       // Blank line (no-op)
    UNKNOWN      // Unrecognized command
};

struct Command {
    CommandType type = CommandType::EMPTY;
    std::string text;     // For STRING
    int delay_ms = 0;     // For DELAY
    std::string key;      // For KEY
    std::string modifier; // For MOD_KEY
    std::string raw;      // Original line, useful for diagnostics
};

// Parse a multi-line script into a list of commands.
// Throws std::invalid_argument on malformed commands.
std::vector<Command> parse(const std::string& script);

// Helpers for named special keys (ENTER, TAB, ESC, etc.).
bool is_special_key(const std::string& name);
uint8_t keycode_for(const std::string& name);

} // namespace macro
