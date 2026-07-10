// Host-side unit tests for sim::HumanSim.
// Uses an injected clock and a mock backend — no Arduino or FreeRTOS required.

#include "test_harness.h"
#include "../src/human_sim.h"
#include "../src/hid/ihid_backend.h"
#include <string>
#include <vector>

// --- Mock backend -----------------------------------------------------------

class MockBackend : public hid::IHidBackend {
public:
    std::string sent_chars;
    std::vector<uint8_t> sent_keys;

    bool begin() override { return true; }
    bool is_connected() override { return true; }
    bool send_char(char c) override { sent_chars += c; return true; }
    bool send_string(const std::string& s) override { sent_chars += s; return true; }
    bool send_key(uint8_t k, uint8_t /*mod*/ = 0) override { sent_keys.push_back(k); return true; }
    void release_all() override {}
};

// --- Shared clock -----------------------------------------------------------

static unsigned long g_fake_time = 0;
static unsigned long fake_clock() { return g_fake_time; }

// ---------------------------------------------------------------------------

static void test_disabled_returns_false() {
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    g_fake_time = 0;
    // Default config has enabled=false
    ASSERT_FALSE(sim.tick());
    ASSERT_FALSE(sim.is_enabled());
}

static void test_enable_tick_returns_true() {
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled = true;
    sim.set_config(cfg);
    g_fake_time = 0;
    ASSERT_TRUE(sim.tick());
    ASSERT_TRUE(sim.is_enabled());
}

static void test_first_burst_starts_immediately() {
    // The sim is constructed with next_ms_=0, so the first burst fires right
    // away without waiting for pause_ms. Two ticks at t=0: the first transitions
    // PAUSING→TYPING, the second types the first character.
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled         = true;
    cfg.pause_ms        = 99999; // large — would delay if applied to first burst
    cfg.char_delay_ms   = 100;
    cfg.words_per_burst = 1;
    sim.set_config(cfg);

    g_fake_time = 0; sim.tick(); // PAUSING -> TYPING (next_ms_ already 0)
    g_fake_time = 0; sim.tick(); // TYPING: send first char (t >= next_ms_=0)
    ASSERT_FALSE(be.sent_chars.empty());
}

static void test_second_burst_waits_pause_ms() {
    // After one full type+erase cycle, the next burst must wait pause_ms.
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled         = true;
    cfg.pause_ms        = 1000;
    cfg.char_delay_ms   = 1;    // fast typing so we exhaust the burst quickly
    cfg.words_per_burst = 1;
    sim.set_config(cfg);

    // Drive through one complete type+erase cycle (burst starts at t=0,
    // erasing completes well before t=500 for a short word).
    for (int t = 0; t <= 500; ++t) {
        g_fake_time = static_cast<unsigned long>(t);
        sim.tick();
    }
    size_t chars_after_first_cycle = be.sent_chars.size();

    // Now we should be in PAUSING phase waiting for pause_ms=1000.
    // No new chars should appear between t=501 and t=1498.
    g_fake_time = 501;  sim.tick();
    g_fake_time = 1498; sim.tick();
    ASSERT_EQ(be.sent_chars.size(), chars_after_first_cycle);

    // At t=1500 (t=0 of erase completion + 1000 ms) the burst should begin.
    // Drive a couple ticks to confirm new chars appear.
    for (int t = 1499; t <= 1502; ++t) {
        g_fake_time = static_cast<unsigned long>(t);
        sim.tick();
    }
    ASSERT_GT(static_cast<int>(be.sent_chars.size()),
              static_cast<int>(chars_after_first_cycle));
}

static void test_erasing_sends_backspaces() {
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled         = true;
    cfg.pause_ms        = 0;
    cfg.char_delay_ms   = 10;
    cfg.words_per_burst = 1;
    sim.set_config(cfg);

    // Drive enough ticks to cover TYPING + ERASING for a short word
    for (int t = 0; t <= 500; t += 5) {
        g_fake_time = static_cast<unsigned long>(t);
        sim.tick();
    }

    ASSERT_FALSE(be.sent_chars.empty());
    ASSERT_FALSE(be.sent_keys.empty());
    for (uint8_t k : be.sent_keys) {
        ASSERT_EQ(k, static_cast<uint8_t>(0x2A)); // BACKSPACE
    }
}

static void test_backspace_count_equals_typed() {
    // Use large pause_ms so only one burst fits in the test window.
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled         = true;
    cfg.pause_ms        = 99999; // second burst won't start within 2000 ticks
    cfg.char_delay_ms   = 1;
    cfg.words_per_burst = 1;
    sim.set_config(cfg);

    // Drive through one full typing+erasing cycle
    for (int t = 0; t <= 2000; ++t) {
        g_fake_time = static_cast<unsigned long>(t);
        sim.tick();
    }

    ASSERT_GT(static_cast<int>(be.sent_chars.size()), 0);
    // Every typed char must be erased exactly once in a single complete cycle
    ASSERT_EQ(be.sent_chars.size(), be.sent_keys.size());
}

static void test_disable_mid_burst_stops_output() {
    MockBackend be;
    sim::HumanSim sim(&be, fake_clock);
    sim::HumanSim::Config cfg;
    cfg.enabled         = true;
    cfg.pause_ms        = 0;
    cfg.char_delay_ms   = 1;
    cfg.words_per_burst = 2;
    sim.set_config(cfg);

    g_fake_time = 0; sim.tick();
    g_fake_time = 5; sim.tick();

    cfg.enabled = false;
    sim.set_config(cfg);

    size_t chars_at_disable = be.sent_chars.size();

    for (int t = 6; t <= 20; ++t) {
        g_fake_time = static_cast<unsigned long>(t);
        sim.tick();
    }

    ASSERT_EQ(be.sent_chars.size(), chars_at_disable);
    ASSERT_FALSE(sim.is_enabled());
}

static void test_longer_burst_produces_more_chars() {
    MockBackend be1, be2;
    sim::HumanSim sim1(&be1, fake_clock);
    sim::HumanSim sim2(&be2, fake_clock);

    sim::HumanSim::Config cfg;
    cfg.enabled = true; cfg.pause_ms = 0; cfg.char_delay_ms = 1;

    cfg.words_per_burst = 1; sim1.set_config(cfg);
    cfg.words_per_burst = 5; sim2.set_config(cfg);

    // Drive through the typing phase for both sims (erase has not started yet
    // for the longer burst at t=200; that's fine — we just compare typed chars).
    for (int t = 0; t <= 200; ++t) {
        g_fake_time = static_cast<unsigned long>(t);
        sim1.tick();
        sim2.tick();
    }

    ASSERT_GT(static_cast<int>(be2.sent_chars.size()),
              static_cast<int>(be1.sent_chars.size()));
}

// ---------------------------------------------------------------------------

int main() {
    RUN_TEST(test_disabled_returns_false);
    RUN_TEST(test_enable_tick_returns_true);
    RUN_TEST(test_first_burst_starts_immediately);
    RUN_TEST(test_second_burst_waits_pause_ms);
    RUN_TEST(test_erasing_sends_backspaces);
    RUN_TEST(test_backspace_count_equals_typed);
    RUN_TEST(test_disable_mid_burst_stops_output);
    RUN_TEST(test_longer_burst_produces_more_chars);
    return test::summary();
}
