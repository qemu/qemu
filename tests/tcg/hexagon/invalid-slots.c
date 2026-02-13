/*
 * Test that invalid slot assignments are properly rejected.
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

char mem[8] __attribute__((aligned(8)));

/*
 * Invalid packet with 2 instructions at slot 0:
 * - Word 0: 0xa1804100 = memw(r0) = r1
 * - Word 1: 0x28032804 = { r3 = #0; r4 = #0 }
 *
 * This should raise SIGILL due to the invalid slot assignment.
 */
static int test_invalid_slots(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "r0 = #mem\n"
        ".word 0xa1804100\n" /* { memw(r0) = r1;      */
        ".word 0x28032804\n" /*   r3 = #0; r4 = #0 }  */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "r3", "r4", "memory");

    return sig;
}

int main()
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    assert(test_invalid_slots() == SIGILL);

    puts("PASS");
    return EXIT_SUCCESS;
}
