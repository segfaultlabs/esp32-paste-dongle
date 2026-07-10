#include "test_harness.h"
#include "../src/macro_runner.h"
#include "../src/macro_parser.h"
#include "../src/keymap.h"

#include <string>

using namespace macro;

static unsigned long g_clock = 0;
static std::function<unsigned long()> fake_clock() {
    return [] { return g_clock; };
}

class MockBackend : public hid::IHidBackend {
public:
    std::string output;
    std::vector<std::pair<uint8_t,uint8_t>> keys; // {keycode, modifiers}
    bool released = false;

    bool begin()                                        override { return true; }
    bool is_connected()                                 override { return true; }
    bool send_char(char ch)                             override { output += ch; return true; }
    bool send_string(const std::string& t)              override { output += t; return true; }
    bool send_key(uint8_t k, uint8_t m = 0)            override { keys.push_back({k, m}); return true; }
    void release_all()                                  override { released = true; }
};

static void test_string_command() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("STRING Hello"), "US");

    ASSERT_TRUE(r.is_running());
    r.tick();
    ASSERT_EQ(b.output, "Hello");
    ASSERT_FALSE(r.is_running());
}

static void test_delay_command() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("DELAY 500\nSTRING done"), "US");

    r.tick();                    // starts the delay
    ASSERT_EQ(b.output, "");     // nothing typed yet
    ASSERT_TRUE(r.is_running());

    g_clock = 300;
    r.tick();                    // still waiting
    ASSERT_EQ(b.output, "");

    g_clock = 600;
    r.tick();                    // delay expired — advance to STRING
    r.tick();                    // execute STRING
    ASSERT_EQ(b.output, "done");
    ASSERT_FALSE(r.is_running());
}

static void test_enter_key() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("ENTER"), "US");
    r.tick();
    ASSERT_EQ((int)b.keys.size(), 1);
    ASSERT_EQ(b.keys[0].first, 0x28u); // ENTER keycode
}

static void test_mod_key() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("CTRL a"), "US");
    r.tick();
    ASSERT_EQ((int)b.keys.size(), 1);
    ASSERT_EQ(b.keys[0].second, 0x01u); // CTRL modifier
}

static void test_multi_command() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("STRING foo\nENTER\nSTRING bar"), "US");

    while (r.tick()) {}
    ASSERT_EQ(b.output, "foobar");
    ASSERT_EQ((int)b.keys.size(), 1);
    ASSERT_FALSE(r.is_running());
}

static void test_cancel() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("STRING abc\nSTRING def"), "US");

    r.tick();                    // execute "abc"
    ASSERT_EQ(b.output, "abc");
    r.cancel();
    ASSERT_FALSE(r.is_running());
    ASSERT_TRUE(b.released);
    r.tick();                    // should be no-op after cancel
    ASSERT_EQ(b.output, "abc");
}

static void test_empty_script() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse(""), "US");
    ASSERT_FALSE(r.is_running());
}

static void test_commands_done_counter() {
    g_clock = 0;
    MockBackend b;
    Runner r(&b, fake_clock());
    r.start(parse("STRING a\nSTRING b\nSTRING c"), "US");
    ASSERT_EQ(r.commands_total(), 3);
    ASSERT_EQ(r.commands_done(), 0);
    r.tick(); ASSERT_EQ(r.commands_done(), 1);
    r.tick(); ASSERT_EQ(r.commands_done(), 2);
    r.tick(); ASSERT_EQ(r.commands_done(), 3);
}

int main() {
    RUN_TEST(test_string_command);
    RUN_TEST(test_delay_command);
    RUN_TEST(test_enter_key);
    RUN_TEST(test_mod_key);
    RUN_TEST(test_multi_command);
    RUN_TEST(test_cancel);
    RUN_TEST(test_empty_script);
    RUN_TEST(test_commands_done_counter);
    return test::summary();
}
