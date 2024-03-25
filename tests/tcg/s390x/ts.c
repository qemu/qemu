/*
 * Test the TEST AND SET instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

static int ts(char *p)
{
    int cc;

    asm("ts %[p]\n"
        "ipm %[cc]"
        : [cc] "=r" (cc)
        , [p] "+Q" (*p)
        : : "cc");

    return (cc >> 28) & 3;
}

int main(void)
{
    char c;

    c = 0x80;
    assert(ts(&c) == 1);
    assert(c == 0xff);

    c = 0x7f;
    assert(ts(&c) == 0);
    assert(c == 0xff);

    return EXIT_SUCCESS;
}
