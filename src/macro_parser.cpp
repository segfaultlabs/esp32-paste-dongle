#include "macro_parser.h"

#include <cctype>
#include <stdexcept>
#include <sstream>

namespace macro {

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

static std::string uppercase(const std::string& s) {
    std::string out;
    for (char c : s) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::vector<Command> parse(const std::string& script) {
    std::vector<Command> result;
    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        Command cmd;
        cmd.raw = line;
        std::string t = trim(line);
        if (t.empty()) {
            cmd.type = CommandType::EMPTY;
            result.push_back(cmd);
            continue;
        }
        if (t[0] == '#') {
            cmd.type = CommandType::COMMENT;
            result.push_back(cmd);
            continue;
        }

        size_t space = t.find(' ');
        std::string keyword = uppercase(space == std::string::npos ? t : t.substr(0, space));
        std::string args = space == std::string::npos ? "" : trim(t.substr(space + 1));

        if (keyword == "STRING") {
            cmd.type = CommandType::STRING;
            cmd.text = args;
        } else if (keyword == "DELAY") {
            cmd.type = CommandType::DELAY;
            cmd.delay_ms = std::stoi(args);
        } else if (keyword == "ENTER" || keyword == "TAB" || keyword == "ESC" ||
                   keyword == "SPACE" || keyword == "BACKSPACE") {
            cmd.type = CommandType::KEY;
            cmd.key = keyword;
        } else if (keyword == "CTRL" || keyword == "ALT" || keyword == "SHIFT" || keyword == "GUI") {
            cmd.type = CommandType::MOD_KEY;
            cmd.modifier = keyword;
            cmd.key = uppercase(args);
        } else {
            cmd.type = CommandType::UNKNOWN;
        }
        result.push_back(cmd);
    }
    return result;
}

bool is_special_key(const std::string& name) {
    const std::string u = uppercase(name);
    return u == "ENTER" || u == "TAB" || u == "ESC" || u == "SPACE" || u == "BACKSPACE";
}

uint8_t keycode_for(const std::string& name) {
    const std::string u = uppercase(name);
    if (u == "ENTER") return 0x28;
    if (u == "ESC") return 0x29;
    if (u == "BACKSPACE") return 0x2A;
    if (u == "TAB") return 0x2B;
    if (u == "SPACE") return 0x2C;
    return 0;
}

} // namespace macro
