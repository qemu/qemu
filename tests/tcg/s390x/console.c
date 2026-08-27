/*
 * Console code for multiarch tests.
 * Reuses the pc-bios/s390-ccw implementation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../../../pc-bios/s390-ccw/sclp.c"
#include "string.h"


void *
memcpy(void *dest, const void *src, size_t n)
{
    char *cdest;
    const char *csrc = src;

    cdest = dest;
    while (n-- > 0) {
        *cdest++ = *csrc++;
    }

    return dest;
}

void *
memset(void *dest, int c, size_t size)
{
    unsigned char *d = (unsigned char *)dest;

    while (size-- > 0) {
        *d++ = (unsigned char)c;
    }

    return dest;
}

void __sys_outc(char c)
{
    write(1, &c, sizeof(c));
}
