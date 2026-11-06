#pragma once

#include <math.h>
#include <stdio.h>

static int g_test_checks;
static int g_test_failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        g_test_checks++;                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_test_failures++;                                              \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                            \
    do {                                                                   \
        double check_a_ = (double)(actual);                                \
        double check_e_ = (double)(expected);                              \
        double check_t_ = (double)(tolerance);                             \
        g_test_checks++;                                                   \
        if (!(fabs(check_a_ - check_e_) <= check_t_)) {                    \
            fprintf(stderr,                                                \
                    "FAIL %s:%d: %s = %.6f, expected %.6f +/- %.6f\n",     \
                    __FILE__, __LINE__, #actual, check_a_, check_e_,       \
                    check_t_);                                             \
            g_test_failures++;                                             \
        }                                                                  \
    } while (0)

static inline int test_report(const char *name)
{
    if (g_test_failures == 0) {
        printf("PASS %s (%d checks)\n", name, g_test_checks);
        return 0;
    }
    printf("FAIL %s (%d/%d checks failed)\n", name, g_test_failures,
           g_test_checks);
    return 1;
}
