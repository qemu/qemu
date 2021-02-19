/*
 * Memory tagging, basic fail cases, asynchronous signals.
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

void pass(int sig, siginfo_t *info, void *uc)
{
    assert(info->si_code == SEGV_MTEAERR);
    exit(0);
}

int main(int ac, char **av)
{
    struct sigaction sa;
    long *p0, *p1, *p2;
    long excl = 1;

    enable_mte(PR_MTE_TCF_ASYNC);
    p0 = alloc_mte_mem(sizeof(*p0));

    /* Create two differently tagged pointers.  */
    asm("irg %0,%1,%2" : "=r"(p1) : "r"(p0), "r"(excl));
    asm("gmi %0,%1,%0" : "+r"(excl) : "r" (p1));
    assert(excl != 1);
    asm("irg %0,%1,%2" : "=r"(p2) : "r"(p0), "r"(excl));
    assert(p1 != p2);

    /* Store the tag from the first pointer.  */
    asm("stg %0, [%0]" : : "r"(p1));

    *p1 = 0;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pass;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    /*
     * Signal for async error will happen eventually.
     * For a real kernel this should be after the next IRQ (e.g. timer).
     * For qemu linux-user, we kick the cpu and exit at the next TB.
     * In either case, loop until this happens (or killed by timeout).
     * For extra sauce, yield, producing EXCP_YIELD to cpu_loop().
     */
    asm("str %0, [%0]; yield" : : "r"(p2));
    while (1);
}
