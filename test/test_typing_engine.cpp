#include "test_harness.h"
#include "../src/typing_engine.h"

#include <cmath>

using namespace typing;

static int g_fake_clock = 0;

static Config test_config(Mode mode, int base_ms) {
    Config cfg;
    cfg.mode = mode;
    cfg.base_delay_ms = base_ms;
    cfg.clock_ms = [] { return g_fake_clock; };
    return cfg;
}

static void test_max_speed_is_fast() {
    Config cfg = test_config(Mode::MAX_SPEED, 1000);
    Engine engine(cfg);
    ASSERT_EQ(engine.next_delay_ms(), 1);
    ASSERT_EQ(engine.next_delay_ms(), 1);
}

static void test_wpm_calculation() {
    Config cfg = test_config(Mode::FAST_TYPIST, 100);
    Engine engine(cfg);
    // 100 ms/char = 600 cpm = 120 wpm (5 chars/word)
    ASSERT_EQ(engine.wpm(), 120);

    cfg.base_delay_ms = 200; // 300 cpm = 60 wpm
    engine.set_config(cfg);
    ASSERT_EQ(engine.wpm(), 60);
}

static void test_jitter_within_range() {
    Config cfg = test_config(Mode::HUMAN, 100);
    cfg.jitter_percent = 20; // +/- 20 ms
    cfg.burst_interval = 0;  // Disable burst pauses for this test.
    Engine engine(cfg);

    for (int i = 0; i < 50; ++i) {
        int d = engine.next_delay_ms();
        ASSERT_GE(d, 80);
        ASSERT_LE(d, 120);
    }
}

static void test_burst_pauses() {
    Config cfg = test_config(Mode::HUMAN, 10);
    cfg.jitter_percent = 0;
    cfg.burst_interval = 5;
    cfg.burst_pause_min_ms = 100;
    cfg.burst_pause_max_ms = 100;
    Engine engine(cfg);

    // First 4 keys should be exactly base delay.
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(engine.next_delay_ms(), 10);
    }
    // 5th key triggers burst pause.
    int d = engine.next_delay_ms();
    ASSERT_EQ(d, 110);
}

static void test_typo_simulation() {
    Config cfg = test_config(Mode::HUMAN, 10);
    cfg.typo_simulation = true;
    cfg.typo_frequency = 3;
    Engine engine(cfg);

    ASSERT_FALSE(engine.should_typo()); // 1
    ASSERT_FALSE(engine.should_typo()); // 2
    ASSERT_TRUE(engine.should_typo());  // 3 -> typo
    ASSERT_FALSE(engine.should_typo()); // reset, 1
}

static void test_typo_disabled() {
    Config cfg = test_config(Mode::HUMAN, 10);
    cfg.typo_simulation = false;
    Engine engine(cfg);

    for (int i = 0; i < 100; ++i) {
        ASSERT_FALSE(engine.should_typo());
    }
}

int main() {
    RUN_TEST(test_max_speed_is_fast);
    RUN_TEST(test_wpm_calculation);
    RUN_TEST(test_jitter_within_range);
    RUN_TEST(test_burst_pauses);
    RUN_TEST(test_typo_simulation);
    RUN_TEST(test_typo_disabled);
    return test::summary();
}
