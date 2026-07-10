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
                   keyword == "SPACE" || keyword == "BACKSPACE" ||
                   keyword == "UP" || keyword == "DOWN" || keyword == "LEFT" || keyword == "RIGHT" ||
                   keyword == "F1"  || keyword == "F2"  || keyword == "F3"  || keyword == "F4"  ||
                   keyword == "F5"  || keyword == "F6"  || keyword == "F7"  || keyword == "F8"  ||
                   keyword == "F9"  || keyword == "F10" || keyword == "F11" || keyword == "F12" ||
                   keyword == "DELETE" || keyword == "HOME" || keyword == "END" ||
                   keyword == "PAGEUP" || keyword == "PAGEDOWN" || keyword == "INSERT") {
            cmd.type = CommandType::KEY;
            cmd.key = keyword;
        } else if (keyword == "MOD_KEY") {
            // MOD_KEY <modifier> <key>  e.g.  MOD_KEY CTRL c
            size_t sp2 = args.find(' ');
            cmd.type = CommandType::MOD_KEY;
            cmd.modifier = uppercase(sp2 == std::string::npos ? args : args.substr(0, sp2));
            cmd.key      = sp2 == std::string::npos ? "" : uppercase(trim(args.substr(sp2 + 1)));
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
    return keycode_for(name) != 0;
}

uint8_t keycode_for(const std::string& name) {
    const std::string u = uppercase(name);
    if (u == "ENTER")    return 0x28;
    if (u == "ESC")      return 0x29;
    if (u == "BACKSPACE")return 0x2A;
    if (u == "TAB")      return 0x2B;
    if (u == "SPACE")    return 0x2C;
    if (u == "RIGHT")    return 0x4F;
    if (u == "LEFT")     return 0x50;
    if (u == "DOWN")     return 0x51;
    if (u == "UP")       return 0x52;
    if (u == "F1")       return 0x3A;
    if (u == "F2")       return 0x3B;
    if (u == "F3")       return 0x3C;
    if (u == "F4")       return 0x3D;
    if (u == "F5")       return 0x3E;
    if (u == "F6")       return 0x3F;
    if (u == "F7")       return 0x40;
    if (u == "F8")       return 0x41;
    if (u == "F9")       return 0x42;
    if (u == "F10")      return 0x43;
    if (u == "F11")      return 0x44;
    if (u == "F12")      return 0x45;
    if (u == "INSERT")   return 0x49;
    if (u == "HOME")     return 0x4A;
    if (u == "PAGEUP")   return 0x4B;
    if (u == "DELETE")   return 0x4C;
    if (u == "END")      return 0x4D;
    if (u == "PAGEDOWN") return 0x4E;
    return 0;
}

} // namespace macro
