/*
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Copyright (C) 2008 IBM Corporation
 * Written by Rusty Russell <rusty@rustcorp.com.au>
 * (Inspired by David Howell's find_next_bit implementation)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"

/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
                            unsigned long offset)
{
    const unsigned long *p = addr + BIT_WORD(offset);
    unsigned long result = offset & ~(BITS_PER_LONG-1);
    unsigned long tmp;

    if (offset >= size) {
        return size;
    }
    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp &= (~0UL << offset);
        if (size < BITS_PER_LONG) {
            goto found_first;
        }
        if (tmp) {
            goto found_middle;
        }
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size >= 4*BITS_PER_LONG) {
        unsigned long d1, d2, d3;
        tmp = *p;
        d1 = *(p+1);
        d2 = *(p+2);
        d3 = *(p+3);
        if (tmp) {
            goto found_middle;
        }
        if (d1 | d2 | d3) {
            break;
        }
        p += 4;
        result += 4*BITS_PER_LONG;
        size -= 4*BITS_PER_LONG;
    }
    while (size >= BITS_PER_LONG) {
        if ((tmp = *(p++))) {
            goto found_middle;
        }
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size) {
        return result;
    }
    tmp = *p;

found_first:
    tmp &= (~0UL >> (BITS_PER_LONG - size));
    if (tmp == 0UL) {		/* Are any bits set? */
        return result + size;	/* Nope. */
    }
found_middle:
    return result + ctzl(tmp);
}

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
                                 unsigned long offset)
{
    const unsigned long *p = addr + BIT_WORD(offset);
    unsigned long result = offset & ~(BITS_PER_LONG-1);
    unsigned long tmp;

    if (offset >= size) {
        return size;
    }
    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp |= ~0UL >> (BITS_PER_LONG - offset);
        if (size < BITS_PER_LONG) {
            goto found_first;
        }
        if (~tmp) {
            goto found_middle;
        }
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG-1)) {
        if (~(tmp = *(p++))) {
            goto found_middle;
        }
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size) {
        return result;
    }
    tmp = *p;

found_first:
    tmp |= ~0UL << size;
    if (tmp == ~0UL) {	/* Are any bits zero? */
        return result + size;	/* Nope. */
    }
found_middle:
    return result + ctzl(~tmp);
}

unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
    unsigned long words;
    unsigned long tmp;

    /* Start at final word. */
    words = size / BITS_PER_LONG;

    /* Partial final word? */
    if (size & (BITS_PER_LONG-1)) {
        tmp = (addr[words] & (~0UL >> (BITS_PER_LONG
                                       - (size & (BITS_PER_LONG-1)))));
        if (tmp) {
            goto found;
        }
    }

    while (words) {
        tmp = addr[--words];
        if (tmp) {
        found:
            return words * BITS_PER_LONG + BITS_PER_LONG - 1 - clzl(tmp);
        }
    }

    /* Not found */
    return size;
}
