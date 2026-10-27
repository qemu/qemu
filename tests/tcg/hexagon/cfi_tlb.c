/*
 * Test a restricted branch interrupted by a data TLB miss.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

static sigjmp_buf cfi_jmp;
static volatile sig_atomic_t sigsegv_count;
static volatile sig_atomic_t sigill_count;
static void *target;

static void *landing_pad(int valid_target)
{
    void *pad;

    asm volatile(
        "p0 = cmp.eq(%[valid_target], #1)\n\t"
        "if (p0) %[pad] = ##valid_cfi_tlb_target\n\t"
        "if (!p0) %[pad] = ##invalid_cfi_tlb_target\n\t"
        : [pad] "=r"(pad) : [valid_target] "r"(valid_target) : "p0");
    return pad;
}

static __attribute__((noinline)) void run_cfi_tlb_test(int valid_target)
{
    asm volatile(
        "p0 = cmp.eq(%[valid_target], #1)\n\t"
        "if (p0) r0 = ##valid_cfi_tlb_target\n\t"
        "if (!p0) r0 = ##invalid_cfi_tlb_target\n\t"
        "r1 = #0\n\t"
        "r2 = #0\n\t"
        ".word 0x52004000\n\t" /* { jumpr_rb r0 */
        ".word 0xa181c200\n\t" /*   memw(r1 + #0) = r2 } */
        "valid_cfi_tlb_target:\n\t"
        ".word 0x7f80c000\n\t" /* nop_rb */
        "jump 1f\n\t"
        "invalid_cfi_tlb_target:\n\t"
        "nop\n\t"
        "1:\n\t"
        : : [valid_target] "r"(valid_target)
        : "r0", "r1", "r2", "p0", "memory");
}

static void handle_sigsegv(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;

    assert(sig == SIGSEGV);
    sigsegv_count++;
    uc->uc_mcontext.pc = (unsigned long)target;
}

static void handle_sigill(int sig, siginfo_t *info, void *context)
{
    assert(sig == SIGILL);
    sigill_count++;
    siglongjmp(cfi_jmp, 1);
}

int main(void)
{
    struct sigaction action;

    action.sa_sigaction = handle_sigsegv;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGSEGV, &action, NULL) == 0);

    action.sa_sigaction = handle_sigill;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &action, NULL) == 0);

    target = landing_pad(1);
    run_cfi_tlb_test(1);
    assert(sigsegv_count == 1);
    assert(sigill_count == 0);

    target = landing_pad(0);
    if (sigsetjmp(cfi_jmp, 1) == 0) {
        run_cfi_tlb_test(0);
        abort();
    }
    assert(sigsegv_count == 2);
    assert(sigill_count == 1);

    puts("PASS");
    return EXIT_SUCCESS;
}
