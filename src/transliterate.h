#pragma once
#include <cstdint>
#include <cstddef>

namespace transliterate {

// Decode one UTF-8 code point starting at s[pos].
// Advances pos by the number of bytes consumed (1-4).
// Returns U+FFFD on invalid or truncated sequences.
uint32_t decode_utf8(const char* s, size_t len, size_t& pos);

// Return an ASCII-only approximation string for the given Unicode code point,
// or nullptr if none is available. The returned pointer is to a literal string.
const char* ascii_for(uint32_t cp);

} // namespace transliterate
