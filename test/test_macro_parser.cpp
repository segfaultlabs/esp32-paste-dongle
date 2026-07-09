#include "test_harness.h"
#include "../src/macro_parser.h"

#include <stdexcept>

using namespace macro;

static void test_empty_and_comments() {
    auto cmds = parse("\n# comment\n  \n");
    ASSERT_EQ(cmds.size(), 3u);
    ASSERT_EQ(cmds[0].type, CommandType::EMPTY);
    ASSERT_EQ(cmds[1].type, CommandType::COMMENT);
    ASSERT_EQ(cmds[2].type, CommandType::EMPTY);
}

static void test_string_command() {
    auto cmds = parse("STRING hello world");
    ASSERT_EQ(cmds.size(), 1u);
    ASSERT_EQ(cmds[0].type, CommandType::STRING);
    ASSERT_EQ(cmds[0].text, "hello world");
}

static void test_delay_command() {
    auto cmds = parse("DELAY 500");
    ASSERT_EQ(cmds.size(), 1u);
    ASSERT_EQ(cmds[0].type, CommandType::DELAY);
    ASSERT_EQ(cmds[0].delay_ms, 500);
}

static void test_special_keys() {
    auto cmds = parse("ENTER\nTAB\nESC\nSPACE\nBACKSPACE");
    ASSERT_EQ(cmds.size(), 5u);
    ASSERT_EQ(cmds[0].type, CommandType::KEY);
    ASSERT_EQ(cmds[0].key, "ENTER");
    ASSERT_EQ(cmds[1].key, "TAB");
    ASSERT_EQ(cmds[2].key, "ESC");
    ASSERT_EQ(cmds[3].key, "SPACE");
    ASSERT_EQ(cmds[4].key, "BACKSPACE");
}

static void test_modifier_key() {
    auto cmds = parse("CTRL a\nALT F4");
    ASSERT_EQ(cmds.size(), 2u);
    ASSERT_EQ(cmds[0].type, CommandType::MOD_KEY);
    ASSERT_EQ(cmds[0].modifier, "CTRL");
    ASSERT_EQ(cmds[0].key, "A");
    ASSERT_EQ(cmds[1].modifier, "ALT");
    ASSERT_EQ(cmds[1].key, "F4");
}

static void test_keycode_lookup() {
    ASSERT_EQ(keycode_for("ENTER"), 0x28);
    ASSERT_EQ(keycode_for("TAB"), 0x2B);
    ASSERT_EQ(keycode_for("SPACE"), 0x2C);
    ASSERT_TRUE(is_special_key("BACKSPACE"));
    ASSERT_FALSE(is_special_key("FOO"));
}

static void test_invalid_delay_throws() {
    try {
        parse("DELAY abc");
        ASSERT_FALSE(true); // should have thrown
    } catch (const std::invalid_argument&) {
        ASSERT_TRUE(true);
    }
}

int main() {
    RUN_TEST(test_empty_and_comments);
    RUN_TEST(test_string_command);
    RUN_TEST(test_delay_command);
    RUN_TEST(test_special_keys);
    RUN_TEST(test_modifier_key);
    RUN_TEST(test_keycode_lookup);
    RUN_TEST(test_invalid_delay_throws);
    return test::summary();
}
