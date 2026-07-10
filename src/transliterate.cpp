#include "transliterate.h"
#include <cstring>

namespace transliterate {

uint32_t decode_utf8(const char* s, size_t len, size_t& pos) {
    if (pos >= len) return 0xFFFD;
    auto b = [&]() -> unsigned char { return static_cast<unsigned char>(s[pos++]); };
    unsigned char c = b();
    if (c < 0x80) return c;
    if (c < 0xC2 || c > 0xF4) return 0xFFFD; // overlong / invalid lead

    uint32_t cp;
    size_t extra;
    if      (c < 0xE0) { cp = c & 0x1F; extra = 1; }
    else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
    else               { cp = c & 0x07; extra = 3; }

    for (size_t i = 0; i < extra; ++i) {
        if (pos >= len) return 0xFFFD;
        unsigned char cont = static_cast<unsigned char>(s[pos]);
        if ((cont & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cont & 0x3F);
        ++pos;
    }
    return cp;
}

struct Entry { uint32_t cp; const char* ascii; };

// Sorted by code point — binary search used for lookup.
static const Entry TABLE[] = {
    // Common whitespace / punctuation that arrives from word processors
    {0x00A0, " "},      // NO-BREAK SPACE
    {0x00A9, "(c)"},    // ©
    {0x00AB, "\""},     // «
    {0x00AE, "(r)"},    // ®
    {0x00B4, "'"},      // ´ acute accent
    {0x00B7, "."},      // · middle dot
    {0x00BB, "\""},     // »
    // Latin-1 supplement — uppercase accented
    {0x00C0, "A"}, {0x00C1, "A"}, {0x00C2, "A"}, {0x00C3, "A"},
    {0x00C4, "A"}, {0x00C5, "A"},
    {0x00C6, "AE"},
    {0x00C7, "C"},
    {0x00C8, "E"}, {0x00C9, "E"}, {0x00CA, "E"}, {0x00CB, "E"},
    {0x00CC, "I"}, {0x00CD, "I"}, {0x00CE, "I"}, {0x00CF, "I"},
    {0x00D0, "D"},
    {0x00D1, "N"},
    {0x00D2, "O"}, {0x00D3, "O"}, {0x00D4, "O"}, {0x00D5, "O"},
    {0x00D6, "O"},
    {0x00D7, "x"},      // × multiplication sign
    {0x00D8, "O"},
    {0x00D9, "U"}, {0x00DA, "U"}, {0x00DB, "U"}, {0x00DC, "U"},
    {0x00DD, "Y"},
    {0x00DF, "ss"},     // ß
    // Latin-1 supplement — lowercase accented
    {0x00E0, "a"}, {0x00E1, "a"}, {0x00E2, "a"}, {0x00E3, "a"},
    {0x00E4, "a"}, {0x00E5, "a"},
    {0x00E6, "ae"},
    {0x00E7, "c"},
    {0x00E8, "e"}, {0x00E9, "e"}, {0x00EA, "e"}, {0x00EB, "e"},
    {0x00EC, "i"}, {0x00ED, "i"}, {0x00EE, "i"}, {0x00EF, "i"},
    {0x00F0, "d"},
    {0x00F1, "n"},
    {0x00F2, "o"}, {0x00F3, "o"}, {0x00F4, "o"}, {0x00F5, "o"},
    {0x00F6, "o"},
    {0x00F7, "/"},      // ÷
    {0x00F8, "o"},
    {0x00F9, "u"}, {0x00FA, "u"}, {0x00FB, "u"}, {0x00FC, "u"},
    {0x00FD, "y"},
    {0x00FF, "y"},
    // Latin Extended-A (common)
    {0x0152, "OE"},     // Œ
    {0x0153, "oe"},     // œ
    {0x0160, "S"},      // Š
    {0x0161, "s"},      // š
    {0x017D, "Z"},      // Ž
    {0x017E, "z"},      // ž
    // General punctuation (word-processor smart chars)
    {0x2013, "-"},      // – en dash
    {0x2014, "--"},     // — em dash
    {0x2018, "'"},      // ' left single quote
    {0x2019, "'"},      // ' right single quote
    {0x201A, ","},      // ‚ single low-9 quotation mark
    {0x201C, "\""},     // " left double quote
    {0x201D, "\""},     // " right double quote
    {0x201E, "\""},     // „ double low-9 quotation mark
    {0x2020, "+"},      // † dagger
    {0x2022, "*"},      // • bullet
    {0x2026, "..."},    // … ellipsis
    {0x2039, "<"},      // ‹
    {0x203A, ">"},      // ›
    // Currency / symbols
    {0x20AC, "EUR"},    // €
    {0x2122, "(tm)"},   // ™
};

static constexpr size_t TABLE_LEN = sizeof(TABLE) / sizeof(TABLE[0]);

const char* ascii_for(uint32_t cp) {
    int lo = 0, hi = static_cast<int>(TABLE_LEN) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (TABLE[mid].cp == cp) return TABLE[mid].ascii;
        if (TABLE[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

} // namespace transliterate
