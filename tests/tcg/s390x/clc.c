/*
 * Test the CLC instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void handle_sigsegv(int sig, siginfo_t *info, void *ucontext)
{
    mcontext_t *mcontext = &((ucontext_t *)ucontext)->uc_mcontext;
    if (mcontext->gregs[0] != 600) {
        write(STDERR_FILENO, "bad r0\n", 7);
        _exit(EXIT_FAILURE);
    }
    if (((mcontext->psw.mask >> 44) & 3) != 1) {
        write(STDERR_FILENO, "bad cc\n", 7);
        _exit(EXIT_FAILURE);
    }
    _exit(EXIT_SUCCESS);
}

int main(void)
{
    register unsigned long r0 asm("r0");
    unsigned long mem = 42, rhs = 500;
    struct sigaction act;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigsegv;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGSEGV, &act, NULL);
    assert(err == 0);

    r0 = 100;
    asm("algr %[r0],%[rhs]\n"
        "clc 0(8,%[mem]),0(0)\n"  /* The 2nd operand will cause a SEGV. */
        : [r0] "+r" (r0)
        : [mem] "r" (&mem)
        , [rhs] "r" (rhs)
        : "cc", "memory");

    return EXIT_FAILURE;
}
