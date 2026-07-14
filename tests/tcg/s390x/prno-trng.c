/*
 * Test that PERFORM RANDOM NUMBER OPERATION TRNG is interruptible.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <asm/ucontext.h>

static unsigned char buf1[16 * 1024 * 1024];
static unsigned char buf2[16 * 1024 * 1024];

static volatile sig_atomic_t interrupted;

static void sigprof_handler(int sig, siginfo_t *info, void *ucontext)
{
    struct ucontext *uc = ucontext;
    unsigned long addr = uc->uc_mcontext.regs.psw.addr;

    if (*(unsigned short *)(addr - 4) == 0xb93c) {
        interrupted++;
    }
}

static void prno_trng(void *b1, unsigned long l1, void *b2, unsigned long l2)
{
    register unsigned long r0 asm("r0") = 114;  /* TRNG */
    register unsigned long r2 asm("r2") = (unsigned long)b1;
    register unsigned long r3 asm("r3") = l1;
    register unsigned long r4 asm("r4") = (unsigned long)b2;
    register unsigned long r5 asm("r5") = l2;

    asm volatile("0: ppno %[r2],%[r4]\n"  /* prno alias for old toolchains */
                 "   jo 0b"
                 : [r2] "+r" (r2), [r3] "+r" (r3)
                 , [r4] "+r" (r4), [r5] "+r" (r5)
                 : "r" (r0)
                 : "cc", "memory");
}

int main(void)
{
    struct itimerval it = {
        .it_interval = { .tv_usec = 10000 },  /* 0.01s */
        .it_value = { .tv_usec = 10000 },
    };
    struct sigaction act = {
        .sa_sigaction = sigprof_handler,
        .sa_flags = SA_SIGINFO,
    };
    int err;

    err = sigaction(SIGPROF, &act, NULL);
    assert(err == 0);
    err = setitimer(ITIMER_PROF, &it, NULL);
    assert(err == 0);

    prno_trng(buf1, sizeof(buf1), buf2, sizeof(buf2));
    printf("interrupted %d times\n", interrupted);
    assert(interrupted >= 3);

    return EXIT_SUCCESS;
}
