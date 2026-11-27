// Minimal test helpers: CHECK records a failure and goes on; main returns the failure count.
#pragma once
#include <cstdio>

namespace kp::test {
inline int g_failures = 0;
inline int g_checks = 0;
}

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++::kp::test::g_checks;                                                        \
        if (!(cond)) {                                                                 \
            ++::kp::test::g_failures;                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                              \
    } while (0)

#define TEST_RESULT(name)                                                              \
    (std::printf("%s: %d checks, %d failed\n", name, ::kp::test::g_checks,             \
                 ::kp::test::g_failures),                                              \
     ::kp::test::g_failures == 0 ? 0 : 1)
