/*
 * Memory tagging, re-reading tag checks.
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

void __attribute__((noinline)) tagset(void *p, size_t size)
{
    size_t i;
    for (i = 0; i < size; i += 16) {
        asm("stg %0, [%0]" : : "r"(p + i));
    }
}

void __attribute__((noinline)) tagcheck(void *p, size_t size)
{
    size_t i;
    void *c;

    for (i = 0; i < size; i += 16) {
        asm("ldg %0, [%1]" : "=r"(c) : "r"(p + i), "0"(p));
        assert(c == p);
    }
}

int main(int ac, char **av)
{
    size_t size = getpagesize() * 4;
    long excl = 1;
    int *p0, *p1;

    enable_mte(PR_MTE_TCF_ASYNC);
    p0 = alloc_mte_mem(size);

    /* Tag the pointer. */
    asm("irg %0,%1,%2" : "=r"(p1) : "r"(p0), "r"(excl));

    tagset(p1, size);
    tagcheck(p1, size);

    return 0;
}
