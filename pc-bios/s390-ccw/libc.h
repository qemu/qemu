/*
 * libc-style definitions and functions
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef S390_CCW_LIBC_H
#define S390_CCW_LIBC_H

typedef unsigned long      size_t;
typedef int                bool;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

static inline void *memset(void *s, int c, size_t n)
{
    size_t i;
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
    size_t i;

    for (i = 0; i < n; i++) {
        dest[i] = src[i];
    }

    return s1;
}

static inline int memcmp(const void *s1, const void *s2, size_t n)
{
    size_t i;
    const uint8_t *p1 = s1, *p2 = s2;

    for (i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] > p2[i] ? 1 : -1;
        }
    }

    return 0;
}

static inline size_t strlen(const char *str)
{
    size_t i;
    for (i = 0; *str; i++) {
        str++;
    }
    return i;
}

static inline char *strcat(char *dest, const char *src)
{
    int i;
    char *dest_end = dest + strlen(dest);

    for (i = 0; i <= strlen(src); i++) {
        dest_end[i] = src[i];
    }
    return dest;
}

static inline int isdigit(int c)
{
    return (c >= '0') && (c <= '9');
}

uint64_t atoui(const char *str);
char *uitoa(uint64_t num, char *str, size_t len);

#endif
