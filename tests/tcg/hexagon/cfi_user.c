/*
 * Test v83 restricted indirect control-flow forms and restricted targets.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cfi_tgts.h"

static sigjmp_buf cfi_jmp;
static volatile sig_atomic_t cfi_signal;

static void handle_sigill(int sig, siginfo_t *info, void *context)
{
    assert(sig == SIGILL);
    cfi_signal = sig;
    siglongjmp(cfi_jmp, 1);
}

int main(void)
{
    struct sigaction action;
    int i;

    action.sa_sigaction = handle_sigill;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &action, NULL) == 0);

    for (i = 0; i < CFI_TEST_CASES; i++) {
        run_test_case(1, i);
    }

    for (i = 0; i < CFI_TEST_CASES; i++) {
        cfi_signal = 0;
        if (sigsetjmp(cfi_jmp, 1) == 0) {
            run_test_case(0, i);
            abort();
        }
        assert(cfi_signal == SIGILL);
    }

    run_jumpr_rb_lr_test();

    cfi_signal = 0;
    if (sigsetjmp(cfi_jmp, 1) == 0) {
        run_jumpr_rb_nop_rb_test();
        abort();
    }
    assert(cfi_signal == SIGILL);

    run_trap0_target_test();

    run_callr_rb_lr_test(1);

    cfi_signal = 0;
    if (sigsetjmp(cfi_jmp, 1) == 0) {
        run_callr_rb_lr_test(0);
        abort();
    }
    assert(cfi_signal == SIGILL);

    run_allocframe_test();

    puts("PASS");
    return EXIT_SUCCESS;
}
