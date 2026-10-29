/*
 * Helpers to run several DSP ASE tests from the same process: assert() is
 * mapped to tap_assert() which only counts failures, and RUN_TEST() reports
 * one TAP result per test, after resetting DSPControl. A test calling
 * RUN_TEST() must end with "return tap_done();".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TEST_DSP_RUNNER_H
#define TEST_DSP_RUNNER_H

#include "../include/tap.h"

#undef assert
#define assert(cond) tap_assert(cond)

#define RUN_TEST(fn) \
    do { \
        int ret_; \
        tap_assert_failures = 0; \
        __asm__ volatile("wrdsp $zero, 0x3f\n\t"); \
        ret_ = fn(); \
        tap_report(!(ret_ || tap_assert_failures), "%s", #fn); \
    } while (0)

#endif
