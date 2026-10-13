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
#include <stdint.h>
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

/* Test multiple post-increment writes to the same GPR */
static int test_post_increment1(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x9b004021    /* {    r1 = memb(r0++#1)      */\n"
        ".word 0x9b00c022    /*      r2 = memb(r0++#1) }    */\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_post_increment2(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x9b00c020    /* r0 = memb(r0++#1) */\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_post_increment3(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x9bc1c020    /* r1:0 = memd(r1++#8) */\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_vreg_legal_predicated(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "r2 = #7\n"
        "r3 = #13\n"
        "p0 = cmp.eq(r2, r3)\n"
        "{\n"
        "    if (p0) v0 = v1\n"
        "    if (!p0) v0 = v2\n"
        "}\n"
        "1:\n"
        "%0 = #23\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "r2", "r3", "p0", "v0", "v1", "memory");

    return sig;
}

/*
 * Complementary predicated writes to the same V-register pair are legal.
 * v16 and v22 are splatted with distinct patterns; since p0 is always
 * true, v14 must end up holding v16's pattern.  Extracting a word from
 * v14 confirms the executed branch actually ran, rather than merely that
 * no SIGILL was raised.
 */
static int test_vreg_pair_legal_predicated(void)
{
    int sig;
    uint32_t result;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%[resume_pc]) = r1\n"
        "r4 = #0x11111111\n"
        "r5 = #0x22222222\n"
        "v16 = vsplat(r4)\n"
        "v22 = vsplat(r5)\n"
        "r4 = #0\n"
        "p0 = cmp.eq(r0, r0)\n"
        "{\n"
        "    if (p0) v15:14 = vcombine(v22, v16)\n"
        "    if (!p0) v15:14 = vcombine(v16, v22)\n"
        "}\n"
        "%[result] = vextract(v14, r4)\n"
        "1:\n"
        "%[sig] = r0\n"
        : [sig] "=r"(sig), [result] "=r"(result)
        : [resume_pc] "r"(&resume_pc)
        : "r0", "r1", "r4", "r5", "p0",
          "v14", "v15", "v16", "v22", "memory");

    return sig == SIGILL ? SIGILL : result;
}

/*
 * A future write may consume a temporary value from the same V-register.
 * buf is zeroed while v0 is initialized non-zero, so v0.tmp loads all-zero
 * and v0.w = vadd(v0.tmp.w, 1) must end up as 1 in every word.  Extracting
 * a word from the result
 * confirms the add actually used the loaded .tmp value, rather than
 * merely that no SIGILL was raised.
 */
static int test_vreg_legal_tmp(void)
{
    long long buf[16] __attribute__((aligned(128)));
    int sig;
    uint32_t result;

    memset(buf, 0, sizeof(buf));

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%[resume_pc]) = r1\n"
        "r2 = %[buf]\n"
        "r3 = #1\n"
        "r4 = #0\n"
        "r5 = #2\n"
        "v0 = vsplat(r5)\n"
        "v1 = vsplat(r3)\n"
        "{\n"
        "    v0.tmp = vmem(r2 + #0)\n"
        "    v0.w = vadd(v0.w, v1.w)\n"
        "}\n"
        "%[result] = vextract(v0, r4)\n"
        "1:\n"
        "%[sig] = r0\n"
        : [sig] "=r"(sig), [result] "=r"(result)
        : [resume_pc] "r"(&resume_pc), [buf] "r"(buf)
        : "r0", "r1", "r2", "r3", "r4", "r5", "v0", "v1", "memory");

    return sig == SIGILL ? SIGILL : result;
}

static int test_vreg_illegal_mixed(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x1a004100    /* { if (p0) v0 = v1 */\n"
        ".word 0x1e03e2e0    /*   v0 = v2  } */\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

/* A pair write and a single write to either pair member are illegal. */
static int test_vreg_illegal_pair_overlap(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%[resume_pc]) = r1\n"
        ".word 0x1f5055ee    /* { v15:14 = vcombine(v21, v16) */\n"
        ".word 0x1e03e5ee    /*   v14 = v5 } */\n"
        "1:\n"
        "%[sig] = r0\n"
        : [sig] "=r"(sig)
        : [resume_pc] "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_vreg_illegal_uncond(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        ".word 0x1e0361e0    /* { v0 = v1 */\n"
        ".word 0x1e03e2e0    /*   v0 = v2  } */\n"
        "1:\n"
        "%0 = r0\n"
        : "=r"(sig)
        : "r"(&resume_pc)
        : "r0", "r1", "memory");

    return sig;
}

static int test_qreg_illegal(void)
{
    int sig;

    asm volatile(
        "r0 = #0\n"
        "r1 = ##1f\n"
        "memw(%1) = r1\n"
        "{\n"
        ".word 0x19a14048    /* { q0 = vand(v0, r1) */\n"
        ".word 0x19a0c044    /*   q0 = vsetq(r0) } */\n"
        "}\n"
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

    assert(test_post_increment1() == SIGILL);
    assert(test_post_increment2() == SIGILL);
    assert(test_post_increment3() == SIGILL);

    assert(test_vreg_legal_predicated() == 23);
    assert(test_vreg_pair_legal_predicated() == 0x11111111);
    assert(test_vreg_legal_tmp() == 1);
    assert(test_vreg_illegal_mixed() == 0);
    assert(test_vreg_illegal_pair_overlap() == 0);
    assert(test_vreg_illegal_uncond() == 0);

    assert(test_qreg_illegal() == 0);

    puts("PASS");
    return EXIT_SUCCESS;
}
