/*
 * Test execution of DC CVADP instruction.
 *
 * Copyright (c) 2023 Zhuojia Shen <chaosdefinition@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/hwcap.h>
#include <sys/auxv.h>

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HWCAP2_DCPODP
#define HWCAP2_DCPODP (1 << 0)
#endif

bool should_fail = false;

static void signal_handler(int sig, siginfo_t *si, void *data)
{
    ucontext_t *uc = (ucontext_t *)data;

    if (should_fail) {
        uc->uc_mcontext.pc += 4;
    } else {
        exit(EXIT_FAILURE);
    }
}

static int do_dc_cvadp(void)
{
    struct sigaction sa = {
        .sa_flags = SA_SIGINFO,
        .sa_sigaction = signal_handler,
    };

    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    asm volatile("dc cvadp, %0\n\t" :: "r"(&sa));

    should_fail = true;
    asm volatile("dc cvadp, %0\n\t" :: "r"(NULL));
    should_fail = false;

    return EXIT_SUCCESS;
}

int main(void)
{
    if (getauxval(AT_HWCAP2) & HWCAP2_DCPODP) {
        return do_dc_cvadp();
    } else {
        printf("SKIP: no HWCAP2_DCPODP on this system\n");
        return EXIT_SUCCESS;
    }
}
