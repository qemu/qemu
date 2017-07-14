/*
 * libc-style definitions and functions
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef S390_CCW_LIBC_H
#define S390_CCW_LIBC_H

typedef long               size_t;
typedef int                bool;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

static inline void *memset(void *s, int c, size_t n)
{
    int i;
    unsigned char *p = s;

    for (i = 0; i < n; i++) {
        p[i] = c;
    }

    return s;
}

static inline void *memcpy(void *s1, const void *s2, size_t n)
{
    uint8_t *dest = s1;
    const uint8_t *src = s2;
    int i;

    for (i = 0; i < n; i++) {
        dest[i] = src[i];
    }

    return s1;
}

#endif
