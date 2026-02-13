/*
 * Test detection of multiple writes to the same register.
 *
 * Ported from the system test (tests/tcg/hexagon/system/multiple_writes.c).
 * In linux-user mode, duplicate GPR writes are detected at translate time
 * and raise SIGILL when at least one conflicting write is unconditional.
 * Purely predicated duplicate writes (e.g., complementary if/if-not) are
 * legal and are not flagged statically.
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
 * Unconditional pair write overlapping a single write:
 *   { r1:0 = add(r3:2, r3:2);  r1 = add(r0, r1) }
 * R1 is written by both instructions.  This is invalid and must raise SIGILL.
 */
static int test_static_pair_overlap(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0xd30242e0\n"  /* r1:0 = add(r3:2, r3:2), parse=01 */
        ".word 0xf300c101\n"  /* r1 = add(r0, r1), parse=11 (end) */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

/*
 * Two predicated writes under complementary predicates:
 *   { if (p0) r0 = r2;  if (!p0) r0 = r3 }
 * This is architecturally valid: only one write executes at runtime.
 * Must NOT raise SIGILL; the result should reflect the executed branch.
 */
static int test_legal_predicated(void)
{
    int result;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "r2 = #7\n"
        "r3 = #13\n"
        "p0 = cmp.eq(r2, r2)\n"
        "{\n"
        "    if (p0) r0 = r2\n"
        "    if (!p0) r0 = r3\n"
        "}\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(result)
        : "r"(&resume_pc)
        : "r0", "r1", "r2", "r3", "p0", "memory");

    return result;
}

/*
 * Mixed: unconditional + predicated writes to the same register:
 *   { if (p0) r1 = add(r0, #0);  if (!p0) r1 = add(r0, #0);
 *     r1 = add(r0, #0) }
 * The unconditional write always conflicts with the predicated writes.
 * Must raise SIGILL.
 */
static int test_mixed_writes(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "p0 = cmp.eq(r0, r0)\n"
        ".word 0x7e204021\n"  /* if (p0) r1 = add(r0, #0), parse=01 */
        ".word 0x7ea04021\n"  /* if (!p0) r1 = add(r0, #0), parse=01 */
        ".word 0x7800c021\n"  /* r1 = add(r0, #0), parse=11 (end) */
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "p0", "memory");

    return sig;
}

/*
 * Zero encoding (issue #2696):
 * The encoding 0x00000000 decodes as a duplex with parse bits
 * [15:14] = 0b00:
 *   slot1: SL1_loadri_io R0 = memw(R0+#0x0)
 *   slot0: SL1_loadri_io R0 = memw(R0+#0x0)
 *
 * Both sub-instructions write R0 unconditionally, which is an invalid
 * packet.  This tests what happens when we jump to zeroed memory.
 * Must raise SIGILL.
 */
static int test_zero(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x00000000\n"
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

    /* Legal: complementary predicated writes must not raise SIGILL */
    assert(test_legal_predicated() == 7);

    /* Illegal: unconditional pair + single overlap must raise SIGILL */
    assert(test_static_pair_overlap() == SIGILL);

    /* Illegal: unconditional + predicated writes to same reg must SIGILL */
    assert(test_mixed_writes() == SIGILL);

    /* Illegal: zero encoding = duplex with duplicate dest R0 */
    assert(test_zero() == SIGILL);

    puts("PASS");
    return EXIT_SUCCESS;
}
