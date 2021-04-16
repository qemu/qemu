/*
 * Memory tagging, faulting unaligned access.
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

void pass(int sig, siginfo_t *info, void *uc)
{
    assert(info->si_code == SEGV_MTESERR);
    exit(0);
}

int main(int ac, char **av)
{
    struct sigaction sa;
    void *p0, *p1, *p2;
    long excl = 1;

    enable_mte(PR_MTE_TCF_SYNC);
    p0 = alloc_mte_mem(sizeof(*p0));

    /* Create two differently tagged pointers.  */
    asm("irg %0,%1,%2" : "=r"(p1) : "r"(p0), "r"(excl));
    asm("gmi %0,%1,%0" : "+r"(excl) : "r" (p1));
    assert(excl != 1);
    asm("irg %0,%1,%2" : "=r"(p2) : "r"(p0), "r"(excl));
    assert(p1 != p2);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pass;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    /* Store store two different tags in sequential granules. */
    asm("stg %0, [%0]" : : "r"(p1));
    asm("stg %0, [%0]" : : "r"(p2 + 16));

    /* Perform an unaligned load crossing the granules. */
    asm volatile("ldr %0, [%1]" : "=r"(p0) : "r"(p1 + 12));
    abort();
}
