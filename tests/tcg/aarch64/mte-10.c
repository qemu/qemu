/*
 * Memory tagging, write-only tag checking
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

void pass(int sig, siginfo_t *info, void *uc)
{
    exit(0);
}

int main(int ac, char **av)
{
    struct sigaction sa;
    int *p0, *p1, *p2;
    long excl = 1;

    enable_mte(PR_MTE_TCF_SYNC | PR_MTE_STORE_ONLY);
    p0 = alloc_mte_mem(sizeof(*p0));

    /* Create two differently tagged pointers. */
    asm("irg %0,%1,%2" : "=r"(p1) : "r"(p0), "r"(excl));
    asm("gmi %0,%1,%0" : "+r"(excl) : "r" (p1));
    assert(excl != 1);
    asm("irg %0,%1,%2" : "=r"(p2) : "r"(p0), "r"(excl));
    assert(p1 != p2);

    /* Store the tag from the first pointer.  */
    asm("stg %0, [%0]" : : "r"(p1));

    /*
     * We write to p1 (stg above makes this check pass) and read from
     * p2 (improperly tagged, but since it's a read, we don't care).
     */
    *p1 = *p2;

    /* enable handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pass;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    /* now we write to badly tagged p2, should fault. */
    *p2 = 0;

    abort();
}
