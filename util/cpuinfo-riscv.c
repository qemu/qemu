/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for RISC-V.
 */

#include "qemu/osdep.h"
#include "host/cpuinfo.h"

unsigned cpuinfo;
static volatile sig_atomic_t got_sigill;

static void sigill_handler(int signo, siginfo_t *si, void *data)
{
    /* Skip the faulty instruction */
    ucontext_t *uc = (ucontext_t *)data;
    uc->uc_mcontext.__gregs[REG_PC] += 4;

    got_sigill = 1;
}

/* Called both as constructor and (possibly) via other constructors. */
unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned left = CPUINFO_ZBA | CPUINFO_ZBB | CPUINFO_ZICOND;
    unsigned info = cpuinfo;

    if (info) {
        return info;
    }

    /* Test for compile-time settings. */
#if defined(__riscv_arch_test) && defined(__riscv_zba)
    info |= CPUINFO_ZBA;
#endif
#if defined(__riscv_arch_test) && defined(__riscv_zbb)
    info |= CPUINFO_ZBB;
#endif
#if defined(__riscv_arch_test) && defined(__riscv_zicond)
    info |= CPUINFO_ZICOND;
#endif
    left &= ~info;

    if (left) {
        struct sigaction sa_old, sa_new;

        memset(&sa_new, 0, sizeof(sa_new));
        sa_new.sa_flags = SA_SIGINFO;
        sa_new.sa_sigaction = sigill_handler;
        sigaction(SIGILL, &sa_new, &sa_old);

        if (left & CPUINFO_ZBA) {
            /* Probe for Zba: add.uw zero,zero,zero. */
            got_sigill = 0;
            asm volatile(".insn r 0x3b, 0, 0x04, zero, zero, zero"
                         : : : "memory");
            info |= got_sigill ? 0 : CPUINFO_ZBA;
            left &= ~CPUINFO_ZBA;
        }

        if (left & CPUINFO_ZBB) {
            /* Probe for Zbb: andn zero,zero,zero. */
            got_sigill = 0;
            asm volatile(".insn r 0x33, 7, 0x20, zero, zero, zero"
                         : : : "memory");
            info |= got_sigill ? 0 : CPUINFO_ZBB;
            left &= ~CPUINFO_ZBB;
        }

        if (left & CPUINFO_ZICOND) {
            /* Probe for Zicond: czero.eqz zero,zero,zero. */
            got_sigill = 0;
            asm volatile(".insn r 0x33, 5, 0x07, zero, zero, zero"
                         : : : "memory");
            info |= got_sigill ? 0 : CPUINFO_ZICOND;
            left &= ~CPUINFO_ZICOND;
        }

        sigaction(SIGILL, &sa_old, NULL);
        assert(left == 0);
    }

    info |= CPUINFO_ALWAYS;
    cpuinfo = info;
    return info;
}
