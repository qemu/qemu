/*
 * Check emulated system register access for linux-user mode.
 *
 * See: https://www.kernel.org/doc/Documentation/arm64/cpu-feature-registers.txt
 *
 * Copyright (c) 2019 Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/hwcap.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>

#ifndef HWCAP_CPUID
#define HWCAP_CPUID (1 << 11)
#endif

/*
 * Older assemblers don't recognize newer system register names,
 * but we can still access them by the Sn_n_Cn_Cn_n syntax.
 * This also means we don't need to specifically request that the
 * assembler enables whatever architectural features the ID registers
 * syntax might be gated behind.
 */
#define SYS_ID_AA64ISAR2_EL1 S3_0_C0_C6_2
#define SYS_ID_AA64MMFR2_EL1 S3_0_C0_C7_2
#define SYS_ID_AA64ZFR0_EL1 S3_0_C0_C4_4
#define SYS_ID_AA64SMFR0_EL1 S3_0_C0_C4_5

int failed_bit_count;

/* Read and print system register `id' value */
#define get_cpu_reg(id) ({                                      \
            unsigned long __val = 0xdeadbeef;                   \
            asm("mrs %0, "#id : "=r" (__val));                  \
            printf("%-20s: 0x%016lx\n", #id, __val);            \
            __val;                                               \
        })

/* As above but also check no bits outside of `mask' are set*/
#define get_cpu_reg_check_mask(id, mask) ({                     \
            unsigned long __cval = get_cpu_reg(id);             \
            unsigned long __extra = __cval & ~mask;             \
            if (__extra) {                                      \
                printf("%-20s: 0x%016lx\n", "  !!extra bits!!", __extra);   \
                failed_bit_count++;                            \
            }                                                   \
})

/* As above but check RAZ */
#define get_cpu_reg_check_zero(id) ({                           \
            unsigned long __val = 0xdeadbeef;                   \
            asm("mrs %0, "#id : "=r" (__val));                  \
            if (__val) {                                        \
                printf("%-20s: 0x%016lx (not RAZ!)\n", #id, __val);        \
                failed_bit_count++;                            \
            }                                                   \
})

/* Chunk up mask into 63:48, 47:32, 31:16, 15:0 to ease counting */
#define _m(a, b, c, d) (0x ## a ## b ## c ## d ##ULL)

bool should_fail;
int should_fail_count;
int should_not_fail_count;
uintptr_t failed_pc[10];

void sigill_handler(int signo, siginfo_t *si, void *data)
{
    ucontext_t *uc = (ucontext_t *)data;

    if (should_fail) {
        should_fail_count++;
    } else {
        uintptr_t pc = (uintptr_t) uc->uc_mcontext.pc;
        failed_pc[should_not_fail_count++] =  pc;
    }
    uc->uc_mcontext.pc += 4;
}

int main(void)
{
    struct sigaction sa;

    /* Hook in a SIGILL handler */
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sigill_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGILL, &sa, 0) != 0) {
        perror("sigaction");
        return 1;
    }

    /* Counter values have been exposed since Linux 4.12 */
    printf("Checking Counter registers\n");

    get_cpu_reg(ctr_el0);
    get_cpu_reg(cntvct_el0);
    get_cpu_reg(cntfrq_el0);

    /* HWCAP_CPUID indicates we can read feature registers, since Linux 4.11 */
    if (!(getauxval(AT_HWCAP) & HWCAP_CPUID)) {
        printf("CPUID registers unavailable\n");
        return 1;
    } else {
        printf("Checking CPUID registers\n");
    }

    /*
     * Some registers only expose some bits to user-space. Anything
     * that is IMPDEF is exported as 0 to user-space. The _mask checks
     * assert no extra bits are set.
     *
     * This check is *not* comprehensive as some fields are set to
     * minimum valid fields - for the purposes of this check allowed
     * to have non-zero values.
     */
    get_cpu_reg_check_mask(id_aa64isar0_el1, _m(f0ff,ffff,f0ff,fff0));
    get_cpu_reg_check_mask(id_aa64isar1_el1, _m(00ff,f0ff,ffff,ffff));
    get_cpu_reg_check_mask(SYS_ID_AA64ISAR2_EL1, _m(00ff,0000,00ff,ffff));
    /* TGran4 & TGran64 as pegged to -1 */
    get_cpu_reg_check_mask(id_aa64mmfr0_el1, _m(f000,0000,ff00,0000));
    get_cpu_reg_check_mask(id_aa64mmfr1_el1, _m(0000,f000,0000,0000));
    get_cpu_reg_check_mask(SYS_ID_AA64MMFR2_EL1, _m(0000,000f,0000,0000));
    /* EL1/EL0 reported as AA64 only */
    get_cpu_reg_check_mask(id_aa64pfr0_el1,  _m(000f,000f,00ff,0011));
    get_cpu_reg_check_mask(id_aa64pfr1_el1,  _m(0000,0000,0f00,0fff));
    /* all hidden, DebugVer fixed to 0x6 (ARMv8 debug architecture) */
    get_cpu_reg_check_mask(id_aa64dfr0_el1,  _m(0000,0000,0000,0006));
    get_cpu_reg_check_zero(id_aa64dfr1_el1);
    get_cpu_reg_check_mask(SYS_ID_AA64ZFR0_EL1,  _m(0ff0,ff0f,0fff,00ff));
    get_cpu_reg_check_mask(SYS_ID_AA64SMFR0_EL1, _m(8ff1,fcff,0000,0000));

    get_cpu_reg_check_zero(id_aa64afr0_el1);
    get_cpu_reg_check_zero(id_aa64afr1_el1);

    get_cpu_reg_check_mask(midr_el1,         _m(0000,0000,ffff,ffff));
    /* mpidr sets bit 31, everything else hidden */
    get_cpu_reg_check_mask(mpidr_el1,        _m(0000,0000,8000,0000));
    /* REVIDR is all IMPDEF so should be all zeros to user-space */
    get_cpu_reg_check_zero(revidr_el1);

    /*
     * There are a block of more registers that are RAZ in the rest of
     * the Op0=3, Op1=0, CRn=0, CRm=0,4,5,6,7 space. However for
     * brevity we don't check stuff that is currently un-allocated
     * here. Feel free to add them ;-)
     */

    printf("Remaining registers should fail\n");
    should_fail = true;

    /* Unexposed register access causes SIGILL */
    get_cpu_reg(id_mmfr0_el1);
    get_cpu_reg(id_mmfr1_el1);
    get_cpu_reg(id_mmfr2_el1);
    get_cpu_reg(id_mmfr3_el1);

    get_cpu_reg(mvfr0_el1);
    get_cpu_reg(mvfr1_el1);

    if (should_not_fail_count > 0) {
        int i;
        for (i = 0; i < should_not_fail_count; i++) {
            uintptr_t pc = failed_pc[i];
            uint32_t insn = *(uint32_t *) pc;
            printf("insn %#x @ %#lx unexpected FAIL\n", insn, pc);
        }
        return 1;
    }

    if (failed_bit_count > 0) {
        printf("Extra information leaked to user-space!\n");
        return 1;
    }

    return should_fail_count == 6 ? 0 : 1;
}
