/*
 * Test that invalid instruction encodings are properly rejected.
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

/*
 * Each test function:
 *   - Sets r0 to something other than SIGILL
 *   - Stores the resume address into resume_pc
 *   - Executes the invalid encoding
 *   - The handler sets r0 = SIGILL and resumes after the faulting packet
 *   - Returns the value in r0
 */

/*
 * Invalid duplex encoding (issue #3291):
 * - Word 0: 0x0fff6fff = immext(#0xfffbffc0), parse bits = 01
 * - Word 1: 0x600237b0 = duplex with:
 *     - slot0 = 0x17b0 (invalid S2 subinstruction encoding)
 *     - slot1 = 0x0002 (valid SA1_addi)
 *     - duplex iclass = 7 (S2 for slot0, A for slot1)
 *
 * Since slot0 doesn't decode to any valid S2 subinstruction, this packet
 * should be rejected and raise SIGILL.
 */
static int test_invalid_duplex(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x0fff6fff\n"  /* immext(#0xfffbffc0), parse=01 */
        ".word 0x600237b0\n"  /* duplex: slot0=0x17b0 (invalid) */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
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

    assert(test_invalid_duplex() == SIGILL);

    puts("PASS");
    return EXIT_SUCCESS;
}
