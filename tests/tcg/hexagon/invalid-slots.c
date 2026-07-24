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

/* Load then indirect jump, load encoded first: no high slot left for jump. */
static int test_invalid_slots_highslot(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "r3 = #mem\n"
        ".word 0x91834006\n" /* { r6 = memw(r3+#0); */
        ".word 0x529fc000\n" /*   jumpr r31 }        */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "r3", "r6", "memory");

    return sig;
}

/*
 * Three predicate-logical ops: each is restricted to slots 2 and 3, so the
 * fourth-and-fifth-slot-free packet still has only two slots for three ops.
 * No change-of-flow is involved, so the only reason to reject it is the slot
 * conflict.
 */
static int test_invalid_slots_crslot23(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x6b024100\n" /* { p0 = and(p1, p2); */
        ".word 0x6b224103\n" /*   p3 = or(p1, p2);   */
        ".word 0x6b42c301\n" /*   p1 = xor(p2, p3) } */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "p0", "p1", "p3", "memory");

    return sig;
}

/* Three transfers plus a duplex: five ops for four slots. */
static int test_invalid_slots_five(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x78004020\n" /* { r0 = #1;           */
        ".word 0x78004041\n" /*   r1 = #2;            */
        ".word 0x78004062\n" /*   r2 = #3;            */
        ".word 0x28452856\n" /*   r5 = #4; r6 = #5 }  */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "r2", "r5", "r6", "memory");

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
    assert(test_invalid_slots_highslot() == SIGILL);
    assert(test_invalid_slots_crslot23() == SIGILL);
    assert(test_invalid_slots_five() == SIGILL);

    puts("PASS");
    return EXIT_SUCCESS;
}
