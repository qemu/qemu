/*
 * Memory tagging, basic pass cases.
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

int main(int ac, char **av)
{
    int *p0, *p1, *p2;
    long c;

    enable_mte(PR_MTE_TCF_NONE);
    p0 = alloc_mte_mem(sizeof(*p0));

    asm("irg %0,%1,%2" : "=r"(p1) : "r"(p0), "r"(1l));
    assert(p1 != p0);
    asm("subp %0,%1,%2" : "=r"(c) : "r"(p0), "r"(p1));
    assert(c == 0);

    asm("stg %0, [%0]" : : "r"(p1));
    asm("ldg %0, [%1]" : "=r"(p2) : "r"(p0), "0"(p0));
    assert(p1 == p2);

    return 0;
}
