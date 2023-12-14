/*
 * Test execution of DC CVAP instruction.
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

#ifndef HWCAP_DCPOP
#define HWCAP_DCPOP (1 << 16)
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

static int do_dc_cvap(void)
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

    asm volatile("dc cvap, %0\n\t" :: "r"(&sa));

    should_fail = true;
    asm volatile("dc cvap, %0\n\t" :: "r"(NULL));
    should_fail = false;

    return EXIT_SUCCESS;
}

int main(void)
{
    if (getauxval(AT_HWCAP) & HWCAP_DCPOP) {
        return do_dc_cvap();
    } else {
        printf("SKIP: no HWCAP_DCPOP on this system\n");
        return EXIT_SUCCESS;
    }
}
