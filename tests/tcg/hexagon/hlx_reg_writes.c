/*
 * Test HLX packet writes:
 * - Repeated L-register writes conflict.
 * - Pair reads use .tmp halves when available.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <signal.h>
#include <unistd.h>

#include "hlx_test.h"

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

/* { L6.h = L0.h; L6.h = L1.h } */
static int test_conflict_single(void)
{
    int sig;

    asm volatile("r0 = #0\n"
                 "r1 = ##1f\n"
                 "memw(%[resume_pc]) = r1\n"
                 ".word 0xe500782c\n"
                 ".word 0xe500f86c\n"
                 "1:\n"
                 "%[sig] = r0\n"
                 : [sig] "=r"(sig)
                 : [resume_pc] "r"(&resume_pc)
                 : "r0", "r1", "memory");
    return sig;
}

/* { L6.h = L1.h; L7:6 = combine(L0, L2) } */
static int test_conflict_pair(void)
{
    int sig;

    asm volatile("r0 = #0\n"
                 "r1 = ##1f\n"
                 "memw(%[resume_pc]) = r1\n"
                 ".word 0xe500786c\n"
                 ".word 0xef24e02d\n"
                 "1:\n"
                 "%[sig] = r0\n"
                 : [sig] "=r"(sig)
                 : [resume_pc] "r"(&resume_pc)
                 : "r0", "r1", "memory");
    return sig;
}

static HLX_Vector zero, live_lo, live_hi, tmp_val, stale;

static void init_vectors(void)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        zero.h[i] = 0;
        live_lo.h[i] = i * 3 + 1;
        live_hi.h[i] = i * 5 + 7;
        tmp_val.h[i] = i * 11 + 2;
        stale.h[i] = 0xdead;
    }
}

static void check_v(const char *name, HLX_Vector got, HLX_Vector exp)
{
    check_h(name, got.h, exp.h, HLX_HALF_ELEMS);
}

/* L4 uses .tmp while L5 uses its live value. */
static void test_tmp_pair_lo(void)
{
    hlx_put_v(0, zero);
    hlx_put_v(2, zero);
    hlx_put_v(3, live_hi);
    hlx_put_v(4, live_lo);
    hlx_put_v(6, tmp_val);
    hlx_put_v(7, stale);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD3 HLX_LD4
                 /* { L5.h = L3.h } */
                 ".word 0xe500f8ea\n"
                 /* { L1.h = L5.h; L5.tmp = mem(r7) } */
                 ".word 0xe5007962\n"
                 ".word 0x9807d005\n"
                 /* { mem(r7) = L5.new } stores zero without changing L5. */
                 ".word 0xa487c005\n"
                 /* { L5:4.w += mpy(L0.h, L2.h); L4.tmp = mem(r6) } */
                 ".word 0xe7046829\n"
                 ".word 0x9806d004\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    check_v("tmp pair lo: L4", hlx_get_v(4), tmp_val);
    check_v("tmp pair lo: L5", hlx_get_v(5), live_hi);
}

/* The packet loads the high half of L5:4 with .tmp. */
static void test_tmp_pair_hi(void)
{
    hlx_put_v(0, zero);
    hlx_put_v(2, zero);
    hlx_put_v(4, live_lo);
    hlx_put_v(5, live_hi);
    hlx_put_v(6, tmp_val);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD4 HLX_LD5
                 /* { L5:4.w += mpy(L0.h, L2.h); L5.tmp = mem(r6) } */
                 ".word 0xe7046829\n"
                 ".word 0x9806d005\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    check_v("tmp pair hi: L4", hlx_get_v(4), live_lo);
    check_v("tmp pair hi: L5", hlx_get_v(5), tmp_val);
}

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &act, NULL);

    if (test_conflict_single() != SIGILL) {
        printf("FAIL conflict single: no SIGILL\n");
        err++;
    }
    if (test_conflict_pair() != SIGILL) {
        printf("FAIL conflict pair: no SIGILL\n");
        err++;
    }

    init_vectors();
    test_tmp_pair_lo();
    test_tmp_pair_hi();

    puts(err ? "FAIL" : "PASS");
    return err;
}
