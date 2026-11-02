/*
 * Bare-metal harness for R5900 tests
 *
 * There is no libc nor any way to exit QEMU on the 'none' machine.
 * Tests end in test_done(), where the gdb script reads the status.
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

int test_failed_line;
const char *test_failed_message;

void __attribute__((noinline, noreturn)) test_done(int status)
{
    for (;;) {
        /* The gdb script stops here */
    }
}

void test_assert_fail(int line)
{
    test_failed_line = line;
    test_done(1);
}

void test_message(const char *msg)
{
    test_failed_message = msg;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;

    while (n--) {
        *p++ = c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = s1, *b = s2;

    for (; n; n--, a++, b++) {
        if (*a != *b) {
            return *a - *b;
        }
    }
    return 0;
}
