/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for RISC-V.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "host/cpuinfo.h"

#ifdef CONFIG_ASM_HWPROBE_H
#include <asm/hwprobe.h>
#include <sys/syscall.h>
#include <asm/unistd.h>
#endif

unsigned cpuinfo;
unsigned riscv_lg2_vlenb;
static volatile sig_atomic_t got_sigill;

static void sigill_handler(int signo, siginfo_t *si, void *data)
{
    /* Skip the faulty instruction */
    ucontext_t *uc = (ucontext_t *)data;

#ifdef __linux__
    uc->uc_mcontext.__gregs[REG_PC] += 4;
#elif defined(__OpenBSD__)
    uc->sc_sepc += 4;
#else
# error Unsupported OS
#endif

    got_sigill = 1;
}

/* Called both as constructor and (possibly) via other constructors. */
unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned left = CPUINFO_ZBA | CPUINFO_ZBB | CPUINFO_ZBS
                  | CPUINFO_ZICOND | CPUINFO_ZVE64X;
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
#if defined(__riscv_arch_test) && defined(__riscv_zbs)
    info |= CPUINFO_ZBS;
#endif
#if defined(__riscv_arch_test) && defined(__riscv_zicond)
    info |= CPUINFO_ZICOND;
#endif
#if defined(__riscv_arch_test) && \
    (defined(__riscv_vector) || defined(__riscv_zve64x))
    info |= CPUINFO_ZVE64X;
#endif
    left &= ~info;

#ifdef CONFIG_ASM_HWPROBE_H
    if (left) {
        /*
         * TODO: glibc 2.40 will introduce <sys/hwprobe.h>, which
         * provides __riscv_hwprobe and __riscv_hwprobe_one,
         * which is a slightly cleaner interface.
         */
        struct riscv_hwprobe pair = { .key = RISCV_HWPROBE_KEY_IMA_EXT_0 };
        if (syscall(__NR_riscv_hwprobe, &pair, 1, 0, NULL, 0) == 0
            && pair.key >= 0) {
            info |= pair.value & RISCV_HWPROBE_EXT_ZBA ? CPUINFO_ZBA : 0;
            info |= pair.value & RISCV_HWPROBE_EXT_ZBB ? CPUINFO_ZBB : 0;
            info |= pair.value & RISCV_HWPROBE_EXT_ZBS ? CPUINFO_ZBS : 0;
            left &= ~(CPUINFO_ZBA | CPUINFO_ZBB | CPUINFO_ZBS);
#ifdef RISCV_HWPROBE_EXT_ZICOND
            info |= pair.value & RISCV_HWPROBE_EXT_ZICOND ? CPUINFO_ZICOND : 0;
            left &= ~CPUINFO_ZICOND;
#endif
            /* For rv64, V is Zve64d, a superset of Zve64x. */
            info |= pair.value & RISCV_HWPROBE_IMA_V ? CPUINFO_ZVE64X : 0;
#ifdef RISCV_HWPROBE_EXT_ZVE64X
            info |= pair.value & RISCV_HWPROBE_EXT_ZVE64X ? CPUINFO_ZVE64X : 0;
#endif
        }
    }
#endif /* CONFIG_ASM_HWPROBE_H */

    /*
     * We only detect support for vectors with hwprobe.  All kernels with
     * support for vectors in userspace also support the hwprobe syscall.
     */
    left &= ~CPUINFO_ZVE64X;

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

        if (left & CPUINFO_ZBS) {
            /* Probe for Zbs: bext zero,zero,zero. */
            got_sigill = 0;
            asm volatile(".insn r 0x33, 5, 0x24, zero, zero, zero"
                         : : : "memory");
            info |= got_sigill ? 0 : CPUINFO_ZBS;
            left &= ~CPUINFO_ZBS;
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

    if (info & CPUINFO_ZVE64X) {
        /*
         * We are guaranteed by RVV-1.0 that VLEN is a power of 2.
         * We are guaranteed by Zve64x that VLEN >= 64, and that
         * EEW of {8,16,32,64} are supported.
         */
        unsigned long vlenb;
        /* csrr %0, vlenb */
        asm volatile(".insn i 0x73, 0x2, %0, zero, -990" : "=r"(vlenb));
        assert(vlenb >= 8);
        assert(is_power_of_2(vlenb));
        /* Cache VLEN in a convenient form. */
        riscv_lg2_vlenb = ctz32(vlenb);
    }

    info |= CPUINFO_ALWAYS;
    cpuinfo = info;
    return info;
}
