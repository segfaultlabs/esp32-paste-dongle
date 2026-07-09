#include "test_harness.h"
#include "../src/keymap.h"

using namespace keymap;

static void test_lowercase_letters() {
    auto r = lookup('a', "US");
    ASSERT_TRUE(r.valid);
    ASSERT_EQ(r.keycode, 0x04);
    ASSERT_EQ(r.modifiers, 0);

    r = lookup('z', "US");
    ASSERT_EQ(r.keycode, 0x1D);
    ASSERT_EQ(r.modifiers, 0);
}

static void test_uppercase_letters() {
    auto r = lookup('A', "US");
    ASSERT_TRUE(r.valid);
    ASSERT_EQ(r.keycode, 0x04);
    ASSERT_EQ(r.modifiers, MOD_SHIFT);
}

static void test_digits() {
    auto r = lookup('5', "US");
    ASSERT_EQ(r.keycode, 0x22);
    ASSERT_EQ(r.modifiers, 0);
}

static void test_shifted_symbols() {
    auto r = lookup('!', "US");
    ASSERT_TRUE(r.valid);
    ASSERT_EQ(r.keycode, 0x1E);
    ASSERT_EQ(r.modifiers, MOD_SHIFT);

    r = lookup('@', "US");
    ASSERT_EQ(r.keycode, 0x1F);
    ASSERT_EQ(r.modifiers, MOD_SHIFT);
}

static void test_whitespace() {
    auto r = lookup(' ', "US");
    ASSERT_EQ(r.keycode, 0x2C);
    ASSERT_EQ(r.modifiers, 0);

    r = lookup('\n', "US");
    ASSERT_EQ(r.keycode, 0x28); // ENTER
    ASSERT_EQ(r.modifiers, 0);
}

static void test_unsupported_character() {
    auto r = lookup('\xFF', "US");
    ASSERT_FALSE(r.valid);
}

static void test_unsupported_layout() {
    auto r = lookup('a', "FR");
    ASSERT_FALSE(r.valid);
}

int main() {
    RUN_TEST(test_lowercase_letters);
    RUN_TEST(test_uppercase_letters);
    RUN_TEST(test_digits);
    RUN_TEST(test_shifted_symbols);
    RUN_TEST(test_whitespace);
    RUN_TEST(test_unsupported_character);
    RUN_TEST(test_unsupported_layout);
    return test::summary();
}
