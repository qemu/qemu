/*
 * Minimal TAP (Test Anything Protocol) helpers for TCG tests.
 *
 * Select the protocol with 'protocol': 'tap' in the meson setup entry.
 * Each test is a single translation unit: the counters are file-local.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TESTS_TCG_TAP_H
#define TESTS_TCG_TAP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool tap_initialized;
static unsigned tap_tests, tap_failures;
static unsigned tap_assert_failures __attribute__((unused));

/* Print the version line; must be called before any other output. */
static inline void tap_start(void)
{
    tap_initialized = true;
    puts("TAP version 14");
}

#define TAP_START() tap_start()

/* Report one result; the description is a printf-style format. */
static inline __attribute__((format(printf, 2, 3)))
void tap_report(int ok, const char *fmt, ...)
{
    va_list ap;

    printf("%s %u - ", ok ? "ok" : "not ok", ++tap_tests);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    tap_failures += !ok;
}

/* Print a diagnostic line, prefixed with '#'; the format is printf-style. */
static inline __attribute__((format(printf, 1, 2)))
void tap_info(const char *fmt, ...)
{
    va_list ap;

    fputs("# ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

#define TAP_INFO(...) tap_info(__VA_ARGS__)

/* Note: the stringified condition must not contain '#'. */
#define TAP_CHECK(cond) tap_report(!!(cond), "%s", #cond)

/*
 * Check a condition without aborting: print a TAP diagnostic ('# ' line)
 * and count the failure, to be consumed by the test reporting its result
 * (see tap_assert_failures).
 */
#define tap_assert(cond) \
    do { \
        if (!(cond)) { \
            TAP_INFO("FAIL %s:%d: %s", __FILE__, __LINE__, #cond); \
            tap_assert_failures++; \
        } \
    } while (0)

/*
 * Print the plan and return the exit status for main(). Abort if TAP_START()
 * was not called, as the output would not be valid TAP.
 */
static inline int tap_done(void)
{
    if (!tap_initialized) {
        puts("Bail out! TAP_START() not called");
        abort();
    }
    printf("1..%u\n", tap_tests);

    return tap_failures ? 1 : 0;
}

#endif
