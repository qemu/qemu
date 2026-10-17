/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test that IDIV of the minimum dividend by -1 raises #DE, which Linux
 * delivers to the guest as SIGFPE with si_code == FPE_INTDIV.
 */
#define _GNU_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static sigjmp_buf idiv_jmp;
static volatile sig_atomic_t idiv_si_code;

static void idiv_sig_handler(int sig, siginfo_t *info, void *puc)
{
    (void)sig;
    (void)puc;
    idiv_si_code = info->si_code;
    siglongjmp(idiv_jmp, 1);
}

static int check_idiv_overflow(int width)
{
    struct sigaction sa, old;
    int ret = 1;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = idiv_sig_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGFPE, &sa, &old);

    idiv_si_code = 0;
    if (sigsetjmp(idiv_jmp, 1) == 0) {
        if (width == 16) {
            /* DX:AX = 0x8000:0x0000, divisor -1 */
            __asm__ volatile ("movl $0x8000, %%edx\n\t"
                              "xorl %%eax, %%eax\n\t"
                              "movl $-1, %%ecx\n\t"
                              "idivw %%cx"
                              : : : "eax", "ecx", "edx", "cc");
        } else {
            /* EDX:EAX = 0x80000000:0x00000000, divisor -1 */
            __asm__ volatile ("movl $0x80000000, %%edx\n\t"
                              "xorl %%eax, %%eax\n\t"
                              "movl $-1, %%ecx\n\t"
                              "idivl %%ecx"
                              : : : "eax", "ecx", "edx", "cc");
        }
        printf("FAIL: idiv%d did not raise #DE\n", width);
    } else if (idiv_si_code != FPE_INTDIV) {
        printf("FAIL: idiv%d raised SIGFPE with si_code %d\n",
               width, (int)idiv_si_code);
    } else {
        ret = 0;
    }

    sigaction(SIGFPE, &old, NULL);
    return ret;
}

int main(void)
{
    int ret = 0;

    ret |= check_idiv_overflow(16);
    ret |= check_idiv_overflow(32);
    return ret;
}
