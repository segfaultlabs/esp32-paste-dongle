// Minimal host-side test harness.
// No external dependencies; compiles with any C++ compiler.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace test {

static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;

inline void check(bool condition, const char* expr, const char* file, int line) {
    ++g_total;
    if (condition) {
        ++g_passed;
    } else {
        ++g_failed;
        std::printf("  FAIL: %s at %s:%d\n", expr, file, line);
    }
}

inline int summary() {
    std::printf("\nTotal: %d, Passed: %d, Failed: %d\n", g_total, g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}

} // namespace test

#define ASSERT_TRUE(expr) test::check((expr), #expr, __FILE__, __LINE__)
#define ASSERT_FALSE(expr) test::check(!(expr), "!(" #expr ")", __FILE__, __LINE__)
#define ASSERT_EQ(a, b) test::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define ASSERT_NE(a, b) test::check((a) != (b), #a " != " #b, __FILE__, __LINE__)
#define ASSERT_LT(a, b) test::check((a) < (b), #a " < " #b, __FILE__, __LINE__)
#define ASSERT_GT(a, b) test::check((a) > (b), #a " > " #b, __FILE__, __LINE__)
#define ASSERT_LE(a, b) test::check((a) <= (b), #a " <= " #b, __FILE__, __LINE__)
#define ASSERT_GE(a, b) test::check((a) >= (b), #a " >= " #b, __FILE__, __LINE__)

#define RUN_TEST(name) \
    do { \
        std::printf("\nRunning: " #name "\n"); \
        name(); \
    } while (0)
