#include "test_harness.h"
#include "../src/paster.h"
#include "../src/typing_engine.h"

#include <string>

using namespace paster;

static int g_clock = 0;

static std::function<int()> fake_clock() {
    return [] { return g_clock; };
}

class MockBackend : public hid::IHidBackend {
public:
    std::string output;
    bool connected = true;

    bool begin() override { return true; }
    bool is_connected() override { return connected; }
    bool send_char(char ch) override { output += ch; return true; }
    bool send_string(const std::string& text) override { output += text; return true; }
    bool send_key(uint8_t, uint8_t) override { return true; }
    void release_all() override {}
};

static void test_types_all_chars() {
    g_clock = 0;
    MockBackend backend;
    Paster paster(&backend, typing::preset(typing::Mode::MAX_SPEED), fake_clock());
    paster.start(3);
    paster.feed("abc");

    int guard = 0;
    while (paster.status().state == State::TYPING && guard < 100) {
        g_clock += 10;
        paster.tick();
        ++guard;
    }

    ASSERT_EQ(backend.output, "abc");
    ASSERT_EQ(paster.status().state, State::DONE);
}

static void test_delays() {
    g_clock = 0;
    MockBackend backend;
    typing::Config cfg;
    cfg.mode = typing::Mode::FAST_TYPIST;
    cfg.base_delay_ms = 100;
    cfg.jitter_percent = 0;
    cfg.burst_interval = 0;
    cfg.clock_ms = fake_clock();

    Paster paster(&backend, cfg, fake_clock());
    paster.start(2);
    paster.feed("ab");

    ASSERT_TRUE(paster.tick());      // sends 'a' at t=0
    ASSERT_EQ(backend.output, "a");

    g_clock = 50;
    ASSERT_TRUE(paster.tick());      // not enough time yet
    ASSERT_EQ(backend.output, "a");

    g_clock = 110;
    ASSERT_TRUE(paster.tick());      // sends 'b'
    ASSERT_EQ(backend.output, "ab");

    ASSERT_FALSE(paster.tick());     // done
}

static void test_cancel() {
    g_clock = 0;
    MockBackend backend;
    Paster paster(&backend, typing::preset(typing::Mode::MAX_SPEED), fake_clock());
    paster.start(3);
    paster.feed("abc");

    ASSERT_TRUE(paster.tick());      // 'a'
    ASSERT_EQ(backend.output, "a");

    paster.cancel();
    ASSERT_EQ(paster.status().state, State::CANCELLED);

    // Further ticks should not type anything.
    ASSERT_FALSE(paster.tick());
    ASSERT_EQ(backend.output, "a");
}

static void test_status() {
    g_clock = 0;
    MockBackend backend;
    Paster paster(&backend, typing::preset(typing::Mode::MAX_SPEED), fake_clock());
    paster.start(5);
    paster.feed("ab");

    Status s = paster.status();
    ASSERT_EQ(s.state, State::TYPING);
    ASSERT_EQ(s.total_chars, 5);
    ASSERT_EQ(s.pending, 2);
    ASSERT_EQ(s.chars_typed, 0);

    paster.tick(); // sends 'a'
    s = paster.status();
    ASSERT_EQ(s.pending, 1);
    ASSERT_EQ(s.chars_typed, 1);
}

static void test_waits_for_connection() {
    g_clock = 0;
    MockBackend backend;
    backend.connected = false;
    Paster paster(&backend, typing::preset(typing::Mode::MAX_SPEED), fake_clock());
    paster.start(1);
    paster.feed("a");

    ASSERT_TRUE(paster.tick());       // still waiting
    ASSERT_EQ(backend.output, "");

    backend.connected = true;
    ASSERT_TRUE(paster.tick());       // now sends
    ASSERT_EQ(backend.output, "a");
}

int main() {
    RUN_TEST(test_types_all_chars);
    RUN_TEST(test_delays);
    RUN_TEST(test_cancel);
    RUN_TEST(test_status);
    RUN_TEST(test_waits_for_connection);
    return test::summary();
}
