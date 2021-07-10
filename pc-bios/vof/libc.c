#include "vof.h"

int strlen(const char *s)
{
    int len = 0;

    while (*s != 0) {
        len += 1;
        s += 1;
    }

    return len;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 != 0 && *s2 != 0) {
        if (*s1 != *s2) {
            break;
        }
        s1 += 1;
        s2 += 1;
    }

    return *s1 - *s2;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *cdest;
    const char *csrc = src;

    cdest = dest;
    while (n-- > 0) {
        *cdest++ = *csrc++;
    }

    return dest;
}

int memcmp(const void *ptr1, const void *ptr2, size_t n)
{
    const unsigned char *p1 = ptr1;
    const unsigned char *p2 = ptr2;

    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1 += 1;
        p2 += 1;
    }

    return 0;
}

void *memset(void *dest, int c, size_t size)
{
    unsigned char *d = (unsigned char *)dest;

    while (size-- > 0) {
        *d++ = (unsigned char)c;
    }

    return dest;
}
