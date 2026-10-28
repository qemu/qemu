/*
 * Freestanding assert() for bare-metal R5900 tests
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef R5900_SYSTEM_ASSERT_H
#define R5900_SYSTEM_ASSERT_H

void test_assert_fail(int line) __attribute__((noreturn));
void test_done(int status) __attribute__((noreturn));

#define assert(cond) ((cond) ? (void)0 : test_assert_fail(__LINE__))

#endif
