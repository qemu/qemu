/*
 * Test that privileged and guest-mode instructions raise SIGILL in user mode.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *resume_pc;

static void handle_sigill(int sig, siginfo_t *info, void *puc)
{
    ucontext_t *uc = (ucontext_t *)puc;

    if (sig != SIGILL) {
        _exit(EXIT_FAILURE);
    }

    uc->uc_mcontext.r0 = SIGILL;
    uc->uc_mcontext.pc = (unsigned long)resume_pc;
}

static int test_priv_insn(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%[pc]) = r1\n"
        "stop(r0)\n"
        "1:\n"
        "%[sig] = r0\n"
        : [sig] "=r"(sig)
        : [pc] "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_guest_insn(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%[pc]) = r1\n"
        "r0 = g0\n"
        "1:\n"
        "%[sig] = r0\n"
        : [sig] "=r"(sig)
        : [pc] "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

int main()
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    assert(test_priv_insn() == SIGILL);
    assert(test_guest_insn() == SIGILL);

    puts("PASS");
    return EXIT_SUCCESS;
}
