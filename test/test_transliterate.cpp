#include "test_harness.h"
#include "../src/transliterate.h"
#include <string>
#include <vector>
#include <cstring>

// Helper: decode the full UTF-8 string s and collect all code points.
static std::vector<uint32_t> decode_all(const char* s) {
    std::vector<uint32_t> out;
    size_t pos = 0, len = std::strlen(s);
    while (pos < len) out.push_back(transliterate::decode_utf8(s, len, pos));
    return out;
}

static void test_ascii_passthrough() {
    size_t pos = 0;
    ASSERT_EQ(transliterate::decode_utf8("A", 1, pos), 0x41u);
    ASSERT_EQ(pos, 1u);
    pos = 0;
    ASSERT_EQ(transliterate::decode_utf8("z", 1, pos), 0x7Au);
}

static void test_two_byte_sequence() {
    // é = U+00E9 = 0xC3 0xA9
    const char s[] = "\xC3\xA9";
    size_t pos = 0;
    ASSERT_EQ(transliterate::decode_utf8(s, 2, pos), 0x00E9u);
    ASSERT_EQ(pos, 2u);
}

static void test_three_byte_sequence() {
    // em dash — = U+2014 = 0xE2 0x80 0x94
    const char s[] = "\xE2\x80\x94";
    size_t pos = 0;
    ASSERT_EQ(transliterate::decode_utf8(s, 3, pos), 0x2014u);
    ASSERT_EQ(pos, 3u);
}

static void test_multi_codepoint_string() {
    // "café" = c a f é (UTF-8: 63 61 66 C3 A9)
    const char s[] = "caf\xC3\xA9";
    auto cps = decode_all(s);
    ASSERT_EQ(cps.size(), 4u);
    ASSERT_EQ(cps[0], 0x63u); // c
    ASSERT_EQ(cps[3], 0x00E9u); // é
}

static void test_invalid_sequence_returns_replacement() {
    // Bare continuation byte is invalid
    const char s[] = "\x80";
    size_t pos = 0;
    uint32_t cp = transliterate::decode_utf8(s, 1, pos);
    ASSERT_EQ(cp, 0xFFFDu);
    ASSERT_EQ(pos, 1u); // consumed the bad byte
}

static void test_ascii_for_accented() {
    ASSERT_TRUE(transliterate::ascii_for(0x00E9) != nullptr); // é → "e"
    ASSERT_EQ(std::string(transliterate::ascii_for(0x00E9)), "e");
    ASSERT_EQ(std::string(transliterate::ascii_for(0x00FC)), "u"); // ü
    ASSERT_EQ(std::string(transliterate::ascii_for(0x00DF)), "ss"); // ß
}

static void test_ascii_for_smart_quotes() {
    ASSERT_TRUE(transliterate::ascii_for(0x2018) != nullptr); // '
    ASSERT_EQ(std::string(transliterate::ascii_for(0x2018)), "'");
    ASSERT_EQ(std::string(transliterate::ascii_for(0x201C)), "\""); // "
    ASSERT_EQ(std::string(transliterate::ascii_for(0x2014)), "--"); // em dash
    ASSERT_EQ(std::string(transliterate::ascii_for(0x2026)), "..."); // …
}

static void test_ascii_for_symbols() {
    ASSERT_EQ(std::string(transliterate::ascii_for(0x00A9)), "(c)"); // ©
    ASSERT_EQ(std::string(transliterate::ascii_for(0x20AC)), "EUR"); // €
    ASSERT_EQ(std::string(transliterate::ascii_for(0x2122)), "(tm)"); // ™
}

static void test_ascii_for_unknown_returns_null() {
    // CJK character — no transliteration
    ASSERT_TRUE(transliterate::ascii_for(0x4E2D) == nullptr); // 中
    ASSERT_TRUE(transliterate::ascii_for(0x1F600) == nullptr); // 😀
}

static void test_nbsp_maps_to_space() {
    const char* r = transliterate::ascii_for(0x00A0);
    ASSERT_TRUE(r != nullptr);
    ASSERT_EQ(std::string(r), " ");
}

int main() {
    RUN_TEST(test_ascii_passthrough);
    RUN_TEST(test_two_byte_sequence);
    RUN_TEST(test_three_byte_sequence);
    RUN_TEST(test_multi_codepoint_string);
    RUN_TEST(test_invalid_sequence_returns_replacement);
    RUN_TEST(test_ascii_for_accented);
    RUN_TEST(test_ascii_for_smart_quotes);
    RUN_TEST(test_ascii_for_symbols);
    RUN_TEST(test_ascii_for_unknown_returns_null);
    RUN_TEST(test_nbsp_maps_to_space);
    return test::summary();
}
