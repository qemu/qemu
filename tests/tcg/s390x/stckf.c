/*
 * Test that a faulting STORE CLOCK FAST does not clobber the condition code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void handle_sigsegv(int sig, siginfo_t *info, void *ucontext)
{
    mcontext_t *mcontext = &((ucontext_t *)ucontext)->uc_mcontext;

    /* The condition code must be the one set by SLGR, not garbage. */
    _exit(((mcontext->psw.mask >> 44) & 3) == 3 ? EXIT_SUCCESS : EXIT_FAILURE);
}

int main(void)
{
    struct sigaction act = {
        .sa_sigaction = handle_sigsegv,
        .sa_flags = SA_SIGINFO,
    };
    int err;

    err = sigaction(SIGSEGV, &act, NULL);
    assert(err == 0);

    asm volatile(
        "lghi %%r1,100\n"
        "lghi %%r2,0\n"
        "clgr %%r1,%%r2\n"  /* CC_OP_LTUGTU_64 */
                            /* cc_src=100 is not valid for CC_OP_SUBU */
        "ipm %%r0\n"        /* force cc_src to env */
        "lghi %%r3,5\n"
        "lghi %%r4,3\n"
        "slgr %%r3,%%r4\n"  /* CC_OP_SUBU, cc=3 */
        "lghi %%r5,0\n"
        "stckf 0(%%r5)\n"   /* faults; cc must stay 3 */
        : : : "r0", "r1", "r2", "r3", "r4", "r5", "cc", "memory");

    return EXIT_FAILURE;
}
