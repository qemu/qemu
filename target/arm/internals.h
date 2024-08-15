/*
 * QEMU ARM CPU -- internal functions and types
 *
 * Copyright (c) 2014 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This header defines functions, types, etc which need to be shared
 * between different source files within target/arm/ but which are
 * private to it and not required by the rest of QEMU.
 */

#ifndef TARGET_ARM_INTERNALS_H
#define TARGET_ARM_INTERNALS_H

#include "exec/breakpoint.h"
#include "hw/registerfields.h"
#include "tcg/tcg-gvec-desc.h"
#include "syndrome.h"
#include "cpu-features.h"

/* register banks for CPU modes */
#define BANK_USRSYS 0
#define BANK_SVC    1
#define BANK_ABT    2
#define BANK_UND    3
#define BANK_IRQ    4
#define BANK_FIQ    5
#define BANK_HYP    6
#define BANK_MON    7

static inline int arm_env_mmu_index(CPUARMState *env)
{
    return EX_TBFLAG_ANY(env->hflags, MMUIDX);
}

static inline bool excp_is_internal(int excp)
{
    /* Return true if this exception number represents a QEMU-internal
     * exception that will not be passed to the guest.
     */
    return excp == EXCP_INTERRUPT
        || excp == EXCP_HLT
        || excp == EXCP_DEBUG
        || excp == EXCP_HALTED
        || excp == EXCP_EXCEPTION_EXIT
        || excp == EXCP_KERNEL_TRAP
        || excp == EXCP_SEMIHOST;
}

/*
 * Default frequency for the generic timer, in Hz.
 * ARMv8.6 and later CPUs architecturally must use a 1GHz timer; before
 * that it was an IMPDEF choice, and QEMU initially picked 62.5MHz,
 * which gives a 16ns tick period.
 *
 * We will use the back-compat value:
 *  - for QEMU CPU types added before we standardized on 1GHz
 *  - for versioned machine types with a version of 9.0 or earlier
 * In any case, the machine model may override via the cntfrq property.
 */
#define GTIMER_DEFAULT_HZ 1000000000
#define GTIMER_BACKCOMPAT_HZ 62500000

/* Bit definitions for the v7M CONTROL register */
FIELD(V7M_CONTROL, NPRIV, 0, 1)
FIELD(V7M_CONTROL, SPSEL, 1, 1)
FIELD(V7M_CONTROL, FPCA, 2, 1)
FIELD(V7M_CONTROL, SFPA, 3, 1)

/* Bit definitions for v7M exception return payload */
FIELD(V7M_EXCRET, ES, 0, 1)
FIELD(V7M_EXCRET, RES0, 1, 1)
FIELD(V7M_EXCRET, SPSEL, 2, 1)
FIELD(V7M_EXCRET, MODE, 3, 1)
FIELD(V7M_EXCRET, FTYPE, 4, 1)
FIELD(V7M_EXCRET, DCRS, 5, 1)
FIELD(V7M_EXCRET, S, 6, 1)
FIELD(V7M_EXCRET, RES1, 7, 25) /* including the must-be-1 prefix */

/* Minimum value which is a magic number for exception return */
#define EXC_RETURN_MIN_MAGIC 0xff000000
/* Minimum number which is a magic number for function or exception return
 * when using v8M security extension
 */
#define FNC_RETURN_MIN_MAGIC 0xfefffffe

/* Bit definitions for DBGWCRn and DBGWCRn_EL1 */
FIELD(DBGWCR, E, 0, 1)
FIELD(DBGWCR, PAC, 1, 2)
FIELD(DBGWCR, LSC, 3, 2)
FIELD(DBGWCR, BAS, 5, 8)
FIELD(DBGWCR, HMC, 13, 1)
FIELD(DBGWCR, SSC, 14, 2)
FIELD(DBGWCR, LBN, 16, 4)
FIELD(DBGWCR, WT, 20, 1)
FIELD(DBGWCR, MASK, 24, 5)
FIELD(DBGWCR, SSCE, 29, 1)

#define VTCR_NSW (1u << 29)
#define VTCR_NSA (1u << 30)
#define VSTCR_SW VTCR_NSW
#define VSTCR_SA VTCR_NSA

/* Bit definitions for CPACR (AArch32 only) */
FIELD(CPACR, CP10, 20, 2)
FIELD(CPACR, CP11, 22, 2)
FIELD(CPACR, TRCDIS, 28, 1)    /* matches CPACR_EL1.TTA */
FIELD(CPACR, D32DIS, 30, 1)    /* up to v7; RAZ in v8 */
FIELD(CPACR, ASEDIS, 31, 1)

/* Bit definitions for CPACR_EL1 (AArch64 only) */
FIELD(CPACR_EL1, ZEN, 16, 2)
FIELD(CPACR_EL1, FPEN, 20, 2)
FIELD(CPACR_EL1, SMEN, 24, 2)
FIELD(CPACR_EL1, TTA, 28, 1)   /* matches CPACR.TRCDIS */

/* Bit definitions for HCPTR (AArch32 only) */
FIELD(HCPTR, TCP10, 10, 1)
FIELD(HCPTR, TCP11, 11, 1)
FIELD(HCPTR, TASE, 15, 1)
FIELD(HCPTR, TTA, 20, 1)
FIELD(HCPTR, TAM, 30, 1)       /* matches CPTR_EL2.TAM */
FIELD(HCPTR, TCPAC, 31, 1)     /* matches CPTR_EL2.TCPAC */

/* Bit definitions for CPTR_EL2 (AArch64 only) */
FIELD(CPTR_EL2, TZ, 8, 1)      /* !E2H */
FIELD(CPTR_EL2, TFP, 10, 1)    /* !E2H, matches HCPTR.TCP10 */
FIELD(CPTR_EL2, TSM, 12, 1)    /* !E2H */
FIELD(CPTR_EL2, ZEN, 16, 2)    /* E2H */
FIELD(CPTR_EL2, FPEN, 20, 2)   /* E2H */
FIELD(CPTR_EL2, SMEN, 24, 2)   /* E2H */
FIELD(CPTR_EL2, TTA, 28, 1)
FIELD(CPTR_EL2, TAM, 30, 1)    /* matches HCPTR.TAM */
FIELD(CPTR_EL2, TCPAC, 31, 1)  /* matches HCPTR.TCPAC */

/* Bit definitions for CPTR_EL3 (AArch64 only) */
FIELD(CPTR_EL3, EZ, 8, 1)
FIELD(CPTR_EL3, TFP, 10, 1)
FIELD(CPTR_EL3, ESM, 12, 1)
FIELD(CPTR_EL3, TTA, 20, 1)
FIELD(CPTR_EL3, TAM, 30, 1)
FIELD(CPTR_EL3, TCPAC, 31, 1)

#define MDCR_MTPME    (1U << 28)
#define MDCR_TDCC     (1U << 27)
#define MDCR_HLP      (1U << 26)  /* MDCR_EL2 */
#define MDCR_SCCD     (1U << 23)  /* MDCR_EL3 */
#define MDCR_HCCD     (1U << 23)  /* MDCR_EL2 */
#define MDCR_EPMAD    (1U << 21)
#define MDCR_EDAD     (1U << 20)
#define MDCR_TTRF     (1U << 19)
#define MDCR_STE      (1U << 18)  /* MDCR_EL3 */
#define MDCR_SPME     (1U << 17)  /* MDCR_EL3 */
#define MDCR_HPMD     (1U << 17)  /* MDCR_EL2 */
#define MDCR_SDD      (1U << 16)
#define MDCR_SPD      (3U << 14)
#define MDCR_TDRA     (1U << 11)
#define MDCR_TDOSA    (1U << 10)
#define MDCR_TDA      (1U << 9)
#define MDCR_TDE      (1U << 8)
#define MDCR_HPME     (1U << 7)
#define MDCR_TPM      (1U << 6)
#define MDCR_TPMCR    (1U << 5)
#define MDCR_HPMN     (0x1fU)

/* Not all of the MDCR_EL3 bits are present in the 32-bit SDCR */
#define SDCR_VALID_MASK (MDCR_MTPME | MDCR_TDCC | MDCR_SCCD | \
                         MDCR_EPMAD | MDCR_EDAD | MDCR_TTRF | \
                         MDCR_STE | MDCR_SPME | MDCR_SPD)

#define TTBCR_N      (7U << 0) /* TTBCR.EAE==0 */
#define TTBCR_T0SZ   (7U << 0) /* TTBCR.EAE==1 */
#define TTBCR_PD0    (1U << 4)
#define TTBCR_PD1    (1U << 5)
#define TTBCR_EPD0   (1U << 7)
#define TTBCR_IRGN0  (3U << 8)
#define TTBCR_ORGN0  (3U << 10)
#define TTBCR_SH0    (3U << 12)
#define TTBCR_T1SZ   (3U << 16)
#define TTBCR_A1     (1U << 22)
#define TTBCR_EPD1   (1U << 23)
#define TTBCR_IRGN1  (3U << 24)
#define TTBCR_ORGN1  (3U << 26)
#define TTBCR_SH1    (1U << 28)
#define TTBCR_EAE    (1U << 31)

FIELD(VTCR, T0SZ, 0, 6)
FIELD(VTCR, SL0, 6, 2)
FIELD(VTCR, IRGN0, 8, 2)
FIELD(VTCR, ORGN0, 10, 2)
FIELD(VTCR, SH0, 12, 2)
FIELD(VTCR, TG0, 14, 2)
FIELD(VTCR, PS, 16, 3)
FIELD(VTCR, VS, 19, 1)
FIELD(VTCR, HA, 21, 1)
FIELD(VTCR, HD, 22, 1)
FIELD(VTCR, HWU59, 25, 1)
FIELD(VTCR, HWU60, 26, 1)
FIELD(VTCR, HWU61, 27, 1)
FIELD(VTCR, HWU62, 28, 1)
FIELD(VTCR, NSW, 29, 1)
FIELD(VTCR, NSA, 30, 1)
FIELD(VTCR, DS, 32, 1)
FIELD(VTCR, SL2, 33, 1)

#define HCRX_ENAS0    (1ULL << 0)
#define HCRX_ENALS    (1ULL << 1)
#define HCRX_ENASR    (1ULL << 2)
#define HCRX_FNXS     (1ULL << 3)
#define HCRX_FGTNXS   (1ULL << 4)
#define HCRX_SMPME    (1ULL << 5)
#define HCRX_TALLINT  (1ULL << 6)
#define HCRX_VINMI    (1ULL << 7)
#define HCRX_VFNMI    (1ULL << 8)
#define HCRX_CMOW     (1ULL << 9)
#define HCRX_MCE2     (1ULL << 10)
#define HCRX_MSCEN    (1ULL << 11)

#define HPFAR_NS      (1ULL << 63)

#define HSTR_TTEE (1 << 16)
#define HSTR_TJDBX (1 << 17)

/*
 * Depending on the value of HCR_EL2.E2H, bits 0 and 1
 * have different bit definitions, and EL1PCTEN might be
 * bit 0 or bit 10. We use _E2H1 and _E2H0 suffixes to
 * disambiguate if necessary.
 */
FIELD(CNTHCTL, EL0PCTEN_E2H1, 0, 1)
FIELD(CNTHCTL, EL0VCTEN_E2H1, 1, 1)
FIELD(CNTHCTL, EL1PCTEN_E2H0, 0, 1)
FIELD(CNTHCTL, EL1PCEN_E2H0, 1, 1)
FIELD(CNTHCTL, EVNTEN, 2, 1)
FIELD(CNTHCTL, EVNTDIR, 3, 1)
FIELD(CNTHCTL, EVNTI, 4, 4)
FIELD(CNTHCTL, EL0VTEN, 8, 1)
FIELD(CNTHCTL, EL0PTEN, 9, 1)
FIELD(CNTHCTL, EL1PCTEN_E2H1, 10, 1)
FIELD(CNTHCTL, EL1PTEN, 11, 1)
FIELD(CNTHCTL, ECV, 12, 1)
FIELD(CNTHCTL, EL1TVT, 13, 1)
FIELD(CNTHCTL, EL1TVCT, 14, 1)
FIELD(CNTHCTL, EL1NVPCT, 15, 1)
FIELD(CNTHCTL, EL1NVVCT, 16, 1)
FIELD(CNTHCTL, EVNTIS, 17, 1)
FIELD(CNTHCTL, CNTVMASK, 18, 1)
FIELD(CNTHCTL, CNTPMASK, 19, 1)

/* We use a few fake FSR values for internal purposes in M profile.
 * M profile cores don't have A/R format FSRs, but currently our
 * get_phys_addr() code assumes A/R profile and reports failures via
 * an A/R format FSR value. We then translate that into the proper
 * M profile exception and FSR status bit in arm_v7m_cpu_do_interrupt().
 * Mostly the FSR values we use for this are those defined for v7PMSA,
 * since we share some of that codepath. A few kinds of fault are
 * only for M profile and have no A/R equivalent, though, so we have
 * to pick a value from the reserved range (which we never otherwise
 * generate) to use for these.
 * These values will never be visible to the guest.
 */
#define M_FAKE_FSR_NSC_EXEC 0xf /* NS executing in S&NSC memory */
#define M_FAKE_FSR_SFAULT 0xe /* SecureFault INVTRAN, INVEP or AUVIOL */

/**
 * arm_aa32_secure_pl1_0(): Return true if in Secure PL1&0 regime
 *
 * Return true if the CPU is in the Secure PL1&0 translation regime.
 * This requires that EL3 exists and is AArch32 and we are currently
 * Secure. If this is the case then the ARMMMUIdx_E10* apply and
 * mean we are in EL3, not EL1.
 */
static inline bool arm_aa32_secure_pl1_0(CPUARMState *env)
{
    return arm_feature(env, ARM_FEATURE_EL3) &&
        !arm_el_is_aa64(env, 3) && arm_is_secure(env);
}

/**
 * raise_exception: Raise the specified exception.
 * Raise a guest exception with the specified value, syndrome register
 * and target exception level. This should be called from helper functions,
 * and never returns because we will longjump back up to the CPU main loop.
 */
G_NORETURN void raise_exception(CPUARMState *env, uint32_t excp,
                                uint32_t syndrome, uint32_t target_el);

/*
 * Similarly, but also use unwinding to restore cpu state.
 */
G_NORETURN void raise_exception_ra(CPUARMState *env, uint32_t excp,
                                      uint32_t syndrome, uint32_t target_el,
                                      uintptr_t ra);

/*
 * For AArch64, map a given EL to an index in the banked_spsr array.
 * Note that this mapping and the AArch32 mapping defined in bank_number()
 * must agree such that the AArch64<->AArch32 SPSRs have the architecturally
 * mandated mapping between each other.
 */
static inline unsigned int aarch64_banked_spsr_index(unsigned int el)
{
    static const unsigned int map[4] = {
        [1] = BANK_SVC, /* EL1.  */
        [2] = BANK_HYP, /* EL2.  */
        [3] = BANK_MON, /* EL3.  */
    };
    assert(el >= 1 && el <= 3);
    return map[el];
}

/* Map CPU modes onto saved register banks.  */
static inline int bank_number(int mode)
{
    switch (mode) {
    case ARM_CPU_MODE_USR:
    case ARM_CPU_MODE_SYS:
        return BANK_USRSYS;
    case ARM_CPU_MODE_SVC:
        return BANK_SVC;
    case ARM_CPU_MODE_ABT:
        return BANK_ABT;
    case ARM_CPU_MODE_UND:
        return BANK_UND;
    case ARM_CPU_MODE_IRQ:
        return BANK_IRQ;
    case ARM_CPU_MODE_FIQ:
        return BANK_FIQ;
    case ARM_CPU_MODE_HYP:
        return BANK_HYP;
    case ARM_CPU_MODE_MON:
        return BANK_MON;
    }
    g_assert_not_reached();
}

/**
 * r14_bank_number: Map CPU mode onto register bank for r14
 *
 * Given an AArch32 CPU mode, return the index into the saved register
 * banks to use for the R14 (LR) in that mode. This is the same as
 * bank_number(), except for the special case of Hyp mode, where
 * R14 is shared with USR and SYS, unlike its R13 and SPSR.
 * This should be used as the index into env->banked_r14[], and
 * bank_number() used for the index into env->banked_r13[] and
 * env->banked_spsr[].
 */
static inline int r14_bank_number(int mode)
{
    return (mode == ARM_CPU_MODE_HYP) ? BANK_USRSYS : bank_number(mode);
}

void arm_cpu_register(const ARMCPUInfo *info);
void aarch64_cpu_register(const ARMCPUInfo *info);

void register_cp_regs_for_features(ARMCPU *cpu);
void init_cpreg_list(ARMCPU *cpu);

void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu);
void arm_translate_init(void);

void arm_cpu_register_gdb_commands(ARMCPU *cpu);
void aarch64_cpu_register_gdb_commands(ARMCPU *cpu, GString *,
                                       GPtrArray *, GPtrArray *);

void arm_restore_state_to_opc(CPUState *cs,
                              const TranslationBlock *tb,
                              const uint64_t *data);

#ifdef CONFIG_TCG
void arm_cpu_synchronize_from_tb(CPUState *cs, const TranslationBlock *tb);

/* Our implementation of TCGCPUOps::cpu_exec_halt */
bool arm_cpu_exec_halt(CPUState *cs);
#endif /* CONFIG_TCG */

typedef enum ARMFPRounding {
    FPROUNDING_TIEEVEN,
    FPROUNDING_POSINF,
    FPROUNDING_NEGINF,
    FPROUNDING_ZERO,
    FPROUNDING_TIEAWAY,
    FPROUNDING_ODD
} ARMFPRounding;

extern const FloatRoundMode arm_rmode_to_sf_map[6];

static inline FloatRoundMode arm_rmode_to_sf(ARMFPRounding rmode)
{
    assert((unsigned)rmode < ARRAY_SIZE(arm_rmode_to_sf_map));
    return arm_rmode_to_sf_map[rmode];
}

static inline void aarch64_save_sp(CPUARMState *env, int el)
{
    if (env->pstate & PSTATE_SP) {
        env->sp_el[el] = env->xregs[31];
    } else {
        env->sp_el[0] = env->xregs[31];
    }
}

static inline void aarch64_restore_sp(CPUARMState *env, int el)
{
    if (env->pstate & PSTATE_SP) {
        env->xregs[31] = env->sp_el[el];
    } else {
        env->xregs[31] = env->sp_el[0];
    }
}

static inline void update_spsel(CPUARMState *env, uint32_t imm)
{
    unsigned int cur_el = arm_current_el(env);
    /* Update PSTATE SPSel bit; this requires us to update the
     * working stack pointer in xregs[31].
     */
    if (!((imm ^ env->pstate) & PSTATE_SP)) {
        return;
    }
    aarch64_save_sp(env, cur_el);
    env->pstate = deposit32(env->pstate, 0, 1, imm);

    /* We rely on illegal updates to SPsel from EL0 to get trapped
     * at translation time.
     */
    assert(cur_el >= 1 && cur_el <= 3);
    aarch64_restore_sp(env, cur_el);
}

/*
 * arm_pamax
 * @cpu: ARMCPU
 *
 * Returns the implementation defined bit-width of physical addresses.
 * The ARMv8 reference manuals refer to this as PAMax().
 */
unsigned int arm_pamax(ARMCPU *cpu);

/* Return true if extended addresses are enabled.
 * This is always the case if our translation regime is 64 bit,
 * but depends on TTBCR.EAE for 32 bit.
 */
static inline bool extended_addresses_enabled(CPUARMState *env)
{
    uint64_t tcr = env->cp15.tcr_el[arm_is_secure(env) ? 3 : 1];
    if (arm_feature(env, ARM_FEATURE_PMSA) &&
        arm_feature(env, ARM_FEATURE_V8)) {
        return true;
    }
    return arm_el_is_aa64(env, 1) ||
           (arm_feature(env, ARM_FEATURE_LPAE) && (tcr & TTBCR_EAE));
}

/* Update a QEMU watchpoint based on the information the guest has set in the
 * DBGWCR<n>_EL1 and DBGWVR<n>_EL1 registers.
 */
void hw_watchpoint_update(ARMCPU *cpu, int n);
/* Update the QEMU watchpoints for every guest watchpoint. This does a
 * complete delete-and-reinstate of the QEMU watchpoint list and so is
 * suitable for use after migration or on reset.
 */
void hw_watchpoint_update_all(ARMCPU *cpu);
/* Update a QEMU breakpoint based on the information the guest has set in the
 * DBGBCR<n>_EL1 and DBGBVR<n>_EL1 registers.
 */
void hw_breakpoint_update(ARMCPU *cpu, int n);
/* Update the QEMU breakpoints for every guest breakpoint. This does a
 * complete delete-and-reinstate of the QEMU breakpoint list and so is
 * suitable for use after migration or on reset.
 */
void hw_breakpoint_update_all(ARMCPU *cpu);

/* Callback function for checking if a breakpoint should trigger. */
bool arm_debug_check_breakpoint(CPUState *cs);

/* Callback function for checking if a watchpoint should trigger. */
bool arm_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp);

/* Adjust addresses (in BE32 mode) before testing against watchpoint
 * addresses.
 */
vaddr arm_adjust_watchpoint_address(CPUState *cs, vaddr addr, int len);

/* Callback function for when a watchpoint or breakpoint triggers. */
void arm_debug_excp_handler(CPUState *cs);

#if defined(CONFIG_USER_ONLY) || !defined(CONFIG_TCG)
static inline bool arm_is_psci_call(ARMCPU *cpu, int excp_type)
{
    return false;
}
static inline void arm_handle_psci_call(ARMCPU *cpu)
{
    g_assert_not_reached();
}
#else
/* Return true if the r0/x0 value indicates that this SMC/HVC is a PSCI call. */
bool arm_is_psci_call(ARMCPU *cpu, int excp_type);
/* Actually handle a PSCI call */
void arm_handle_psci_call(ARMCPU *cpu);
#endif

/**
 * arm_clear_exclusive: clear the exclusive monitor
 * @env: CPU env
 * Clear the CPU's exclusive monitor, like the guest CLREX instruction.
 */
static inline void arm_clear_exclusive(CPUARMState *env)
{
    env->exclusive_addr = -1;
}

/**
 * ARMFaultType: type of an ARM MMU fault
 * This corresponds to the v8A pseudocode's Fault enumeration,
 * with extensions for QEMU internal conditions.
 */
typedef enum ARMFaultType {
    ARMFault_None,
    ARMFault_AccessFlag,
    ARMFault_Alignment,
    ARMFault_Background,
    ARMFault_Domain,
    ARMFault_Permission,
    ARMFault_Translation,
    ARMFault_AddressSize,
    ARMFault_SyncExternal,
    ARMFault_SyncExternalOnWalk,
    ARMFault_SyncParity,
    ARMFault_SyncParityOnWalk,
    ARMFault_AsyncParity,
    ARMFault_AsyncExternal,
    ARMFault_Debug,
    ARMFault_TLBConflict,
    ARMFault_UnsuppAtomicUpdate,
    ARMFault_Lockdown,
    ARMFault_Exclusive,
    ARMFault_ICacheMaint,
    ARMFault_QEMU_NSCExec, /* v8M: NS executing in S&NSC memory */
    ARMFault_QEMU_SFault, /* v8M: SecureFault INVTRAN, INVEP or AUVIOL */
    ARMFault_GPCFOnWalk,
    ARMFault_GPCFOnOutput,
} ARMFaultType;

typedef enum ARMGPCF {
    GPCF_None,
    GPCF_AddressSize,
    GPCF_Walk,
    GPCF_EABT,
    GPCF_Fail,
} ARMGPCF;

/**
 * ARMMMUFaultInfo: Information describing an ARM MMU Fault
 * @type: Type of fault
 * @gpcf: Subtype of ARMFault_GPCFOn{Walk,Output}.
 * @level: Table walk level (for translation, access flag and permission faults)
 * @domain: Domain of the fault address (for non-LPAE CPUs only)
 * @s2addr: Address that caused a fault at stage 2
 * @paddr: physical address that caused a fault for gpc
 * @paddr_space: physical address space that caused a fault for gpc
 * @stage2: True if we faulted at stage 2
 * @s1ptw: True if we faulted at stage 2 while doing a stage 1 page-table walk
 * @s1ns: True if we faulted on a non-secure IPA while in secure state
 * @ea: True if we should set the EA (external abort type) bit in syndrome
 */
typedef struct ARMMMUFaultInfo ARMMMUFaultInfo;
struct ARMMMUFaultInfo {
    ARMFaultType type;
    ARMGPCF gpcf;
    target_ulong s2addr;
    target_ulong paddr;
    ARMSecuritySpace paddr_space;
    int level;
    int domain;
    bool stage2;
    bool s1ptw;
    bool s1ns;
    bool ea;
};

/**
 * arm_fi_to_sfsc: Convert fault info struct to short-format FSC
 * Compare pseudocode EncodeSDFSC(), though unlike that function
 * we set up a whole FSR-format code including domain field and
 * putting the high bit of the FSC into bit 10.
 */
static inline uint32_t arm_fi_to_sfsc(ARMMMUFaultInfo *fi)
{
    uint32_t fsc;

    switch (fi->type) {
    case ARMFault_None:
        return 0;
    case ARMFault_AccessFlag:
        fsc = fi->level == 1 ? 0x3 : 0x6;
        break;
    case ARMFault_Alignment:
        fsc = 0x1;
        break;
    case ARMFault_Permission:
        fsc = fi->level == 1 ? 0xd : 0xf;
        break;
    case ARMFault_Domain:
        fsc = fi->level == 1 ? 0x9 : 0xb;
        break;
    case ARMFault_Translation:
        fsc = fi->level == 1 ? 0x5 : 0x7;
        break;
    case ARMFault_SyncExternal:
        fsc = 0x8 | (fi->ea << 12);
        break;
    case ARMFault_SyncExternalOnWalk:
        fsc = fi->level == 1 ? 0xc : 0xe;
        fsc |= (fi->ea << 12);
        break;
    case ARMFault_SyncParity:
        fsc = 0x409;
        break;
    case ARMFault_SyncParityOnWalk:
        fsc = fi->level == 1 ? 0x40c : 0x40e;
        break;
    case ARMFault_AsyncParity:
        fsc = 0x408;
        break;
    case ARMFault_AsyncExternal:
        fsc = 0x406 | (fi->ea << 12);
        break;
    case ARMFault_Debug:
        fsc = 0x2;
        break;
    case ARMFault_TLBConflict:
        fsc = 0x400;
        break;
    case ARMFault_Lockdown:
        fsc = 0x404;
        break;
    case ARMFault_Exclusive:
        fsc = 0x405;
        break;
    case ARMFault_ICacheMaint:
        fsc = 0x4;
        break;
    case ARMFault_Background:
        fsc = 0x0;
        break;
    case ARMFault_QEMU_NSCExec:
        fsc = M_FAKE_FSR_NSC_EXEC;
        break;
    case ARMFault_QEMU_SFault:
        fsc = M_FAKE_FSR_SFAULT;
        break;
    default:
        /* Other faults can't occur in a context that requires a
         * short-format status code.
         */
        g_assert_not_reached();
    }

    fsc |= (fi->domain << 4);
    return fsc;
}

/**
 * arm_fi_to_lfsc: Convert fault info struct to long-format FSC
 * Compare pseudocode EncodeLDFSC(), though unlike that function
 * we fill in also the LPAE bit 9 of a DFSR format.
 */
static inline uint32_t arm_fi_to_lfsc(ARMMMUFaultInfo *fi)
{
    uint32_t fsc;

    switch (fi->type) {
    case ARMFault_None:
        return 0;
    case ARMFault_AddressSize:
        assert(fi->level >= -1 && fi->level <= 3);
        if (fi->level < 0) {
            fsc = 0b101001;
        } else {
            fsc = fi->level;
        }
        break;
    case ARMFault_AccessFlag:
        assert(fi->level >= 0 && fi->level <= 3);
        fsc = 0b001000 | fi->level;
        break;
    case ARMFault_Permission:
        assert(fi->level >= 0 && fi->level <= 3);
        fsc = 0b001100 | fi->level;
        break;
    case ARMFault_Translation:
        assert(fi->level >= -1 && fi->level <= 3);
        if (fi->level < 0) {
            fsc = 0b101011;
        } else {
            fsc = 0b000100 | fi->level;
        }
        break;
    case ARMFault_SyncExternal:
        fsc = 0x10 | (fi->ea << 12);
        break;
    case ARMFault_SyncExternalOnWalk:
        assert(fi->level >= -1 && fi->level <= 3);
        if (fi->level < 0) {
            fsc = 0b010011;
        } else {
            fsc = 0b010100 | fi->level;
        }
        fsc |= fi->ea << 12;
        break;
    case ARMFault_SyncParity:
        fsc = 0x18;
        break;
    case ARMFault_SyncParityOnWalk:
        assert(fi->level >= -1 && fi->level <= 3);
        if (fi->level < 0) {
            fsc = 0b011011;
        } else {
            fsc = 0b011100 | fi->level;
        }
        break;
    case ARMFault_AsyncParity:
        fsc = 0x19;
        break;
    case ARMFault_AsyncExternal:
        fsc = 0x11 | (fi->ea << 12);
        break;
    case ARMFault_Alignment:
        fsc = 0x21;
        break;
    case ARMFault_Debug:
        fsc = 0x22;
        break;
    case ARMFault_TLBConflict:
        fsc = 0x30;
        break;
    case ARMFault_UnsuppAtomicUpdate:
        fsc = 0x31;
        break;
    case ARMFault_Lockdown:
        fsc = 0x34;
        break;
    case ARMFault_Exclusive:
        fsc = 0x35;
        break;
    case ARMFault_GPCFOnWalk:
        assert(fi->level >= -1 && fi->level <= 3);
        if (fi->level < 0) {
            fsc = 0b100011;
        } else {
            fsc = 0b100100 | fi->level;
        }
        break;
    case ARMFault_GPCFOnOutput:
        fsc = 0b101000;
        break;
    default:
        /* Other faults can't occur in a context that requires a
         * long-format status code.
         */
        g_assert_not_reached();
    }

    fsc |= 1 << 9;
    return fsc;
}

static inline bool arm_extabort_type(MemTxResult result)
{
    /* The EA bit in syndromes and fault status registers is an
     * IMPDEF classification of external aborts. ARM implementations
     * usually use this to indicate AXI bus Decode error (0) or
     * Slave error (1); in QEMU we follow that.
     */
    return result != MEMTX_DECODE_ERROR;
}

#ifdef CONFIG_USER_ONLY
void arm_cpu_record_sigsegv(CPUState *cpu, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra);
void arm_cpu_record_sigbus(CPUState *cpu, vaddr addr,
                           MMUAccessType access_type, uintptr_t ra);
#else
bool arm_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);
#endif

static inline int arm_to_core_mmu_idx(ARMMMUIdx mmu_idx)
{
    return mmu_idx & ARM_MMU_IDX_COREIDX_MASK;
}

static inline ARMMMUIdx core_to_arm_mmu_idx(CPUARMState *env, int mmu_idx)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        return mmu_idx | ARM_MMU_IDX_M;
    } else {
        return mmu_idx | ARM_MMU_IDX_A;
    }
}

static inline ARMMMUIdx core_to_aa64_mmu_idx(int mmu_idx)
{
    /* AArch64 is always a-profile. */
    return mmu_idx | ARM_MMU_IDX_A;
}

/**
 * Return the exception level we're running at if our current MMU index
 * is @mmu_idx. @s_pl1_0 should be true if this is the AArch32
 * Secure PL1&0 translation regime.
 */
int arm_mmu_idx_to_el(ARMMMUIdx mmu_idx, bool s_pl1_0);

/* Return the MMU index for a v7M CPU in the specified security state */
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate);

/*
 * Return true if the stage 1 translation regime is using LPAE
 * format page tables
 */
bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx);

/* Raise a data fault alignment exception for the specified virtual address */
G_NORETURN void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                            MMUAccessType access_type,
                                            int mmu_idx, uintptr_t retaddr);

#ifndef CONFIG_USER_ONLY
/* arm_cpu_do_transaction_failed: handle a memory system error response
 * (eg "no device/memory present at address") by raising an external abort
 * exception
 */
void arm_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr addr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr);
#endif

/* Call any registered EL change hooks */
static inline void arm_call_pre_el_change_hook(ARMCPU *cpu)
{
    ARMELChangeHook *hook, *next;
    QLIST_FOREACH_SAFE(hook, &cpu->pre_el_change_hooks, node, next) {
        hook->hook(cpu, hook->opaque);
    }
}
static inline void arm_call_el_change_hook(ARMCPU *cpu)
{
    ARMELChangeHook *hook, *next;
    QLIST_FOREACH_SAFE(hook, &cpu->el_change_hooks, node, next) {
        hook->hook(cpu, hook->opaque);
    }
}

/* Return true if this address translation regime has two ranges.  */
static inline bool regime_has_2_ranges(ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_Stage1_E1:
    case ARMMMUIdx_Stage1_E1_PAN:
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return true;
    default:
        return false;
    }
}

static inline bool regime_is_pan(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_Stage1_E1_PAN:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_E20_2_PAN:
        return true;
    default:
        return false;
    }
}

static inline bool regime_is_stage2(ARMMMUIdx mmu_idx)
{
    return mmu_idx == ARMMMUIdx_Stage2 || mmu_idx == ARMMMUIdx_Stage2_S;
}

/* Return the exception level which controls this address translation regime */
static inline uint32_t regime_el(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
    case ARMMMUIdx_Stage2:
    case ARMMMUIdx_Stage2_S:
    case ARMMMUIdx_E2:
        return 2;
    case ARMMMUIdx_E3:
        return 3;
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_Stage1_E1:
    case ARMMMUIdx_Stage1_E1_PAN:
        return arm_el_is_aa64(env, 3) || !arm_is_secure_below_el3(env) ? 1 : 3;
    case ARMMMUIdx_MPrivNegPri:
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MPriv:
    case ARMMMUIdx_MUser:
    case ARMMMUIdx_MSPrivNegPri:
    case ARMMMUIdx_MSUserNegPri:
    case ARMMMUIdx_MSPriv:
    case ARMMMUIdx_MSUser:
        return 1;
    default:
        g_assert_not_reached();
    }
}

static inline bool regime_is_user(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_MUser:
    case ARMMMUIdx_MSUser:
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MSUserNegPri:
        return true;
    default:
        return false;
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
        g_assert_not_reached();
    }
}

/* Return the SCTLR value which controls this address translation regime */
static inline uint64_t regime_sctlr(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    return env->cp15.sctlr_el[regime_el(env, mmu_idx)];
}

/*
 * These are the fields in VTCR_EL2 which affect both the Secure stage 2
 * and the Non-Secure stage 2 translation regimes (and hence which are
 * not present in VSTCR_EL2).
 */
#define VTCR_SHARED_FIELD_MASK \
    (R_VTCR_IRGN0_MASK | R_VTCR_ORGN0_MASK | R_VTCR_SH0_MASK | \
     R_VTCR_PS_MASK | R_VTCR_VS_MASK | R_VTCR_HA_MASK | R_VTCR_HD_MASK | \
     R_VTCR_DS_MASK)

/* Return the value of the TCR controlling this translation regime */
static inline uint64_t regime_tcr(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    if (mmu_idx == ARMMMUIdx_Stage2) {
        return env->cp15.vtcr_el2;
    }
    if (mmu_idx == ARMMMUIdx_Stage2_S) {
        /*
         * Secure stage 2 shares fields from VTCR_EL2. We merge those
         * in with the VSTCR_EL2 value to synthesize a single VTCR_EL2 format
         * value so the callers don't need to special case this.
         *
         * If a future architecture change defines bits in VSTCR_EL2 that
         * overlap with these VTCR_EL2 fields we may need to revisit this.
         */
        uint64_t v = env->cp15.vstcr_el2 & ~VTCR_SHARED_FIELD_MASK;
        v |= env->cp15.vtcr_el2 & VTCR_SHARED_FIELD_MASK;
        return v;
    }
    return env->cp15.tcr_el[regime_el(env, mmu_idx)];
}

/* Return true if the translation regime is using LPAE format page tables */
static inline bool regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    int el = regime_el(env, mmu_idx);
    if (el == 2 || arm_el_is_aa64(env, el)) {
        return true;
    }
    if (arm_feature(env, ARM_FEATURE_PMSA) &&
        arm_feature(env, ARM_FEATURE_V8)) {
        return true;
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)
        && (regime_tcr(env, mmu_idx) & TTBCR_EAE)) {
        return true;
    }
    return false;
}

/**
 * arm_num_brps: Return number of implemented breakpoints.
 * Note that the ID register BRPS field is "number of bps - 1",
 * and we return the actual number of breakpoints.
 */
static inline int arm_num_brps(ARMCPU *cpu)
{
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        return FIELD_EX64(cpu->isar.id_aa64dfr0, ID_AA64DFR0, BRPS) + 1;
    } else {
        return FIELD_EX32(cpu->isar.dbgdidr, DBGDIDR, BRPS) + 1;
    }
}

/**
 * arm_num_wrps: Return number of implemented watchpoints.
 * Note that the ID register WRPS field is "number of wps - 1",
 * and we return the actual number of watchpoints.
 */
static inline int arm_num_wrps(ARMCPU *cpu)
{
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        return FIELD_EX64(cpu->isar.id_aa64dfr0, ID_AA64DFR0, WRPS) + 1;
    } else {
        return FIELD_EX32(cpu->isar.dbgdidr, DBGDIDR, WRPS) + 1;
    }
}

/**
 * arm_num_ctx_cmps: Return number of implemented context comparators.
 * Note that the ID register CTX_CMPS field is "number of cmps - 1",
 * and we return the actual number of comparators.
 */
static inline int arm_num_ctx_cmps(ARMCPU *cpu)
{
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        return FIELD_EX64(cpu->isar.id_aa64dfr0, ID_AA64DFR0, CTX_CMPS) + 1;
    } else {
        return FIELD_EX32(cpu->isar.dbgdidr, DBGDIDR, CTX_CMPS) + 1;
    }
}

/**
 * v7m_using_psp: Return true if using process stack pointer
 * Return true if the CPU is currently using the process stack
 * pointer, or false if it is using the main stack pointer.
 */
static inline bool v7m_using_psp(CPUARMState *env)
{
    /* Handler mode always uses the main stack; for thread mode
     * the CONTROL.SPSEL bit determines the answer.
     * Note that in v7M it is not possible to be in Handler mode with
     * CONTROL.SPSEL non-zero, but in v8M it is, so we must check both.
     */
    return !arm_v7m_is_handler_mode(env) &&
        env->v7m.control[env->v7m.secure] & R_V7M_CONTROL_SPSEL_MASK;
}

/**
 * v7m_sp_limit: Return SP limit for current CPU state
 * Return the SP limit value for the current CPU security state
 * and stack pointer.
 */
static inline uint32_t v7m_sp_limit(CPUARMState *env)
{
    if (v7m_using_psp(env)) {
        return env->v7m.psplim[env->v7m.secure];
    } else {
        return env->v7m.msplim[env->v7m.secure];
    }
}

/**
 * v7m_cpacr_pass:
 * Return true if the v7M CPACR permits access to the FPU for the specified
 * security state and privilege level.
 */
static inline bool v7m_cpacr_pass(CPUARMState *env,
                                  bool is_secure, bool is_priv)
{
    switch (extract32(env->v7m.cpacr[is_secure], 20, 2)) {
    case 0:
    case 2: /* UNPREDICTABLE: we treat like 0 */
        return false;
    case 1:
        return is_priv;
    case 3:
        return true;
    default:
        g_assert_not_reached();
    }
}

/**
 * aarch32_mode_name(): Return name of the AArch32 CPU mode
 * @psr: Program Status Register indicating CPU mode
 *
 * Returns, for debug logging purposes, a printable representation
 * of the AArch32 CPU mode ("svc", "usr", etc) as indicated by
 * the low bits of the specified PSR.
 */
static inline const char *aarch32_mode_name(uint32_t psr)
{
    static const char cpu_mode_names[16][4] = {
        "usr", "fiq", "irq", "svc", "???", "???", "mon", "abt",
        "???", "???", "hyp", "und", "???", "???", "???", "sys"
    };

    return cpu_mode_names[psr & 0xf];
}

/**
 * arm_cpu_update_virq: Update CPU_INTERRUPT_VIRQ bit in cs->interrupt_request
 *
 * Update the CPU_INTERRUPT_VIRQ bit in cs->interrupt_request, following
 * a change to either the input VIRQ line from the GIC or the HCR_EL2.VI bit.
 * Must be called with the BQL held.
 */
void arm_cpu_update_virq(ARMCPU *cpu);

/**
 * arm_cpu_update_vfiq: Update CPU_INTERRUPT_VFIQ bit in cs->interrupt_request
 *
 * Update the CPU_INTERRUPT_VFIQ bit in cs->interrupt_request, following
 * a change to either the input VFIQ line from the GIC or the HCR_EL2.VF bit.
 * Must be called with the BQL held.
 */
void arm_cpu_update_vfiq(ARMCPU *cpu);

/**
 * arm_cpu_update_vinmi: Update CPU_INTERRUPT_VINMI bit in cs->interrupt_request
 *
 * Update the CPU_INTERRUPT_VINMI bit in cs->interrupt_request, following
 * a change to either the input VNMI line from the GIC or the HCRX_EL2.VINMI.
 * Must be called with the BQL held.
 */
void arm_cpu_update_vinmi(ARMCPU *cpu);

/**
 * arm_cpu_update_vfnmi: Update CPU_INTERRUPT_VFNMI bit in cs->interrupt_request
 *
 * Update the CPU_INTERRUPT_VFNMI bit in cs->interrupt_request, following
 * a change to the HCRX_EL2.VFNMI.
 * Must be called with the BQL held.
 */
void arm_cpu_update_vfnmi(ARMCPU *cpu);

/**
 * arm_cpu_update_vserr: Update CPU_INTERRUPT_VSERR bit
 *
 * Update the CPU_INTERRUPT_VSERR bit in cs->interrupt_request,
 * following a change to the HCR_EL2.VSE bit.
 */
void arm_cpu_update_vserr(ARMCPU *cpu);

/**
 * arm_mmu_idx_el:
 * @env: The cpu environment
 * @el: The EL to use.
 *
 * Return the full ARMMMUIdx for the translation regime for EL.
 */
ARMMMUIdx arm_mmu_idx_el(CPUARMState *env, int el);

/**
 * arm_mmu_idx:
 * @env: The cpu environment
 *
 * Return the full ARMMMUIdx for the current translation regime.
 */
ARMMMUIdx arm_mmu_idx(CPUARMState *env);

/**
 * arm_stage1_mmu_idx:
 * @env: The cpu environment
 *
 * Return the ARMMMUIdx for the stage1 traversal for the current regime.
 */
#ifdef CONFIG_USER_ONLY
static inline ARMMMUIdx stage_1_mmu_idx(ARMMMUIdx mmu_idx)
{
    return ARMMMUIdx_Stage1_E0;
}
static inline ARMMMUIdx arm_stage1_mmu_idx(CPUARMState *env)
{
    return ARMMMUIdx_Stage1_E0;
}
#else
ARMMMUIdx stage_1_mmu_idx(ARMMMUIdx mmu_idx);
ARMMMUIdx arm_stage1_mmu_idx(CPUARMState *env);
#endif

/**
 * arm_mmu_idx_is_stage1_of_2:
 * @mmu_idx: The ARMMMUIdx to test
 *
 * Return true if @mmu_idx is a NOTLB mmu_idx that is the
 * first stage of a two stage regime.
 */
static inline bool arm_mmu_idx_is_stage1_of_2(ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_Stage1_E1:
    case ARMMMUIdx_Stage1_E1_PAN:
        return true;
    default:
        return false;
    }
}

static inline uint32_t aarch32_cpsr_valid_mask(uint64_t features,
                                               const ARMISARegisters *id)
{
    uint32_t valid = CPSR_M | CPSR_AIF | CPSR_IL | CPSR_NZCV;

    if ((features >> ARM_FEATURE_V4T) & 1) {
        valid |= CPSR_T;
    }
    if ((features >> ARM_FEATURE_V5) & 1) {
        valid |= CPSR_Q; /* V5TE in reality*/
    }
    if ((features >> ARM_FEATURE_V6) & 1) {
        valid |= CPSR_E | CPSR_GE;
    }
    if ((features >> ARM_FEATURE_THUMB2) & 1) {
        valid |= CPSR_IT;
    }
    if (isar_feature_aa32_jazelle(id)) {
        valid |= CPSR_J;
    }
    if (isar_feature_aa32_pan(id)) {
        valid |= CPSR_PAN;
    }
    if (isar_feature_aa32_dit(id)) {
        valid |= CPSR_DIT;
    }
    if (isar_feature_aa32_ssbs(id)) {
        valid |= CPSR_SSBS;
    }

    return valid;
}

static inline uint32_t aarch64_pstate_valid_mask(const ARMISARegisters *id)
{
    uint32_t valid;

    valid = PSTATE_M | PSTATE_DAIF | PSTATE_IL | PSTATE_SS | PSTATE_NZCV;
    if (isar_feature_aa64_bti(id)) {
        valid |= PSTATE_BTYPE;
    }
    if (isar_feature_aa64_pan(id)) {
        valid |= PSTATE_PAN;
    }
    if (isar_feature_aa64_uao(id)) {
        valid |= PSTATE_UAO;
    }
    if (isar_feature_aa64_dit(id)) {
        valid |= PSTATE_DIT;
    }
    if (isar_feature_aa64_ssbs(id)) {
        valid |= PSTATE_SSBS;
    }
    if (isar_feature_aa64_mte(id)) {
        valid |= PSTATE_TCO;
    }
    if (isar_feature_aa64_nmi(id)) {
        valid |= PSTATE_ALLINT;
    }

    return valid;
}

/* Granule size (i.e. page size) */
typedef enum ARMGranuleSize {
    /* Same order as TG0 encoding */
    Gran4K,
    Gran64K,
    Gran16K,
    GranInvalid,
} ARMGranuleSize;

/**
 * arm_granule_bits: Return address size of the granule in bits
 *
 * Return the address size of the granule in bits. This corresponds
 * to the pseudocode TGxGranuleBits().
 */
static inline int arm_granule_bits(ARMGranuleSize gran)
{
    switch (gran) {
    case Gran64K:
        return 16;
    case Gran16K:
        return 14;
    case Gran4K:
        return 12;
    default:
        g_assert_not_reached();
    }
}

/*
 * Parameters of a given virtual address, as extracted from the
 * translation control register (TCR) for a given regime.
 */
typedef struct ARMVAParameters {
    unsigned tsz    : 8;
    unsigned ps     : 3;
    unsigned sh     : 2;
    unsigned select : 1;
    bool tbi        : 1;
    bool epd        : 1;
    bool hpd        : 1;
    bool tsz_oob    : 1;  /* tsz has been clamped to legal range */
    bool ds         : 1;
    bool ha         : 1;
    bool hd         : 1;
    ARMGranuleSize gran : 2;
} ARMVAParameters;

/**
 * aa64_va_parameters: Return parameters for an AArch64 virtual address
 * @env: CPU
 * @va: virtual address to look up
 * @mmu_idx: determines translation regime to use
 * @data: true if this is a data access
 * @el1_is_aa32: true if we are asking about stage 2 when EL1 is AArch32
 *  (ignored if @mmu_idx is for a stage 1 regime; only affects tsz/tsz_oob)
 */
ARMVAParameters aa64_va_parameters(CPUARMState *env, uint64_t va,
                                   ARMMMUIdx mmu_idx, bool data,
                                   bool el1_is_aa32);

int aa64_va_parameter_tbi(uint64_t tcr, ARMMMUIdx mmu_idx);
int aa64_va_parameter_tbid(uint64_t tcr, ARMMMUIdx mmu_idx);
int aa64_va_parameter_tcma(uint64_t tcr, ARMMMUIdx mmu_idx);

/* Determine if allocation tags are available.  */
static inline bool allocation_tag_access_enabled(CPUARMState *env, int el,
                                                 uint64_t sctlr)
{
    if (el < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_ATA)) {
        return false;
    }
    if (el < 2 && arm_is_el2_enabled(env)) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        if (!(hcr & HCR_ATA) && (!(hcr & HCR_E2H) || !(hcr & HCR_TGE))) {
            return false;
        }
    }
    sctlr &= (el == 0 ? SCTLR_ATA0 : SCTLR_ATA);
    return sctlr != 0;
}

#ifndef CONFIG_USER_ONLY

/* Security attributes for an address, as returned by v8m_security_lookup. */
typedef struct V8M_SAttributes {
    bool subpage; /* true if these attrs don't cover the whole TARGET_PAGE */
    bool ns;
    bool nsc;
    uint8_t sregion;
    bool srvalid;
    uint8_t iregion;
    bool irvalid;
} V8M_SAttributes;

void v8m_security_lookup(CPUARMState *env, uint32_t address,
                         MMUAccessType access_type, ARMMMUIdx mmu_idx,
                         bool secure, V8M_SAttributes *sattrs);

/* Cacheability and shareability attributes for a memory access */
typedef struct ARMCacheAttrs {
    /*
     * If is_s2_format is true, attrs is the S2 descriptor bits [5:2]
     * Otherwise, attrs is the same as the MAIR_EL1 8-bit format
     */
    unsigned int attrs:8;
    unsigned int shareability:2; /* as in the SH field of the VMSAv8-64 PTEs */
    bool is_s2_format:1;
} ARMCacheAttrs;

/* Fields that are valid upon success. */
typedef struct GetPhysAddrResult {
    CPUTLBEntryFull f;
    ARMCacheAttrs cacheattrs;
} GetPhysAddrResult;

/**
 * get_phys_addr: get the physical address for a virtual address
 * @env: CPUARMState
 * @address: virtual address to get physical address for
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index indicating required translation regime
 * @result: set on translation success.
 * @fi: set to fault info if the translation fails
 *
 * Find the physical address corresponding to the given virtual address,
 * by doing a translation table walk on MMU based systems or using the
 * MPU state on MPU based systems.
 *
 * Returns false if the translation was successful. Otherwise, phys_ptr, attrs,
 * prot and page_size may not be filled in, and the populated fsr value provides
 * information on why the translation aborted, in the format of a
 * DFSR/IFSR fault register, with the following caveats:
 *  * we honour the short vs long DFSR format differences.
 *  * the WnR bit is never set (the caller must do this).
 *  * for PSMAv5 based systems we don't bother to return a full FSR format
 *    value.
 */
bool get_phys_addr(CPUARMState *env, target_ulong address,
                   MMUAccessType access_type, ARMMMUIdx mmu_idx,
                   GetPhysAddrResult *result, ARMMMUFaultInfo *fi)
    __attribute__((nonnull));

/**
 * get_phys_addr_with_space_nogpc: get the physical address for a virtual
 *                                 address
 * @env: CPUARMState
 * @address: virtual address to get physical address for
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index indicating required translation regime
 * @space: security space for the access
 * @result: set on translation success.
 * @fi: set to fault info if the translation fails
 *
 * Similar to get_phys_addr, but use the given security space and don't perform
 * a Granule Protection Check on the resulting address.
 */
bool get_phys_addr_with_space_nogpc(CPUARMState *env, target_ulong address,
                                    MMUAccessType access_type,
                                    ARMMMUIdx mmu_idx, ARMSecuritySpace space,
                                    GetPhysAddrResult *result,
                                    ARMMMUFaultInfo *fi)
    __attribute__((nonnull));

bool pmsav8_mpu_lookup(CPUARMState *env, uint32_t address,
                       MMUAccessType access_type, ARMMMUIdx mmu_idx,
                       bool is_secure, GetPhysAddrResult *result,
                       ARMMMUFaultInfo *fi, uint32_t *mregion);

void arm_log_exception(CPUState *cs);

#endif /* !CONFIG_USER_ONLY */

/*
 * SVE predicates are 1/8 the size of SVE vectors, and cannot use
 * the same simd_desc() encoding due to restrictions on size.
 * Use these instead.
 */
FIELD(PREDDESC, OPRSZ, 0, 6)
FIELD(PREDDESC, ESZ, 6, 2)
FIELD(PREDDESC, DATA, 8, 24)

/*
 * The SVE simd_data field, for memory ops, contains either
 * rd (5 bits) or a shift count (2 bits).
 */
#define SVE_MTEDESC_SHIFT 5

/* Bits within a descriptor passed to the helper_mte_check* functions. */
FIELD(MTEDESC, MIDX,  0, 4)
FIELD(MTEDESC, TBI,   4, 2)
FIELD(MTEDESC, TCMA,  6, 2)
FIELD(MTEDESC, WRITE, 8, 1)
FIELD(MTEDESC, ALIGN, 9, 3)
FIELD(MTEDESC, SIZEM1, 12, SIMD_DATA_BITS - SVE_MTEDESC_SHIFT - 12)  /* size - 1 */

bool mte_probe(CPUARMState *env, uint32_t desc, uint64_t ptr);
uint64_t mte_check(CPUARMState *env, uint32_t desc, uint64_t ptr, uintptr_t ra);

/**
 * mte_mops_probe: Check where the next MTE failure is for a FEAT_MOPS operation
 * @env: CPU env
 * @ptr: start address of memory region (dirty pointer)
 * @size: length of region (guaranteed not to cross a page boundary)
 * @desc: MTEDESC descriptor word (0 means no MTE checks)
 * Returns: the size of the region that can be copied without hitting
 *          an MTE tag failure
 *
 * Note that we assume that the caller has already checked the TBI
 * and TCMA bits with mte_checks_needed() and an MTE check is definitely
 * required.
 */
uint64_t mte_mops_probe(CPUARMState *env, uint64_t ptr, uint64_t size,
                        uint32_t desc);

/**
 * mte_mops_probe_rev: Check where the next MTE failure is for a FEAT_MOPS
 *                     operation going in the reverse direction
 * @env: CPU env
 * @ptr: *end* address of memory region (dirty pointer)
 * @size: length of region (guaranteed not to cross a page boundary)
 * @desc: MTEDESC descriptor word (0 means no MTE checks)
 * Returns: the size of the region that can be copied without hitting
 *          an MTE tag failure
 *
 * Note that we assume that the caller has already checked the TBI
 * and TCMA bits with mte_checks_needed() and an MTE check is definitely
 * required.
 */
uint64_t mte_mops_probe_rev(CPUARMState *env, uint64_t ptr, uint64_t size,
                            uint32_t desc);

/**
 * mte_check_fail: Record an MTE tag check failure
 * @env: CPU env
 * @desc: MTEDESC descriptor word
 * @dirty_ptr: Failing dirty address
 * @ra: TCG retaddr
 *
 * This may never return (if the MTE tag checks are configured to fault).
 */
void mte_check_fail(CPUARMState *env, uint32_t desc,
                    uint64_t dirty_ptr, uintptr_t ra);

/**
 * mte_mops_set_tags: Set MTE tags for a portion of a FEAT_MOPS operation
 * @env: CPU env
 * @dirty_ptr: Start address of memory region (dirty pointer)
 * @size: length of region (guaranteed not to cross page boundary)
 * @desc: MTEDESC descriptor word
 */
void mte_mops_set_tags(CPUARMState *env, uint64_t dirty_ptr, uint64_t size,
                       uint32_t desc);

static inline int allocation_tag_from_addr(uint64_t ptr)
{
    return extract64(ptr, 56, 4);
}

static inline uint64_t address_with_allocation_tag(uint64_t ptr, int rtag)
{
    return deposit64(ptr, 56, 4, rtag);
}

/* Return true if tbi bits mean that the access is checked.  */
static inline bool tbi_check(uint32_t desc, int bit55)
{
    return (desc >> (R_MTEDESC_TBI_SHIFT + bit55)) & 1;
}

/* Return true if tcma bits mean that the access is unchecked.  */
static inline bool tcma_check(uint32_t desc, int bit55, int ptr_tag)
{
    /*
     * We had extracted bit55 and ptr_tag for other reasons, so fold
     * (ptr<59:55> == 00000 || ptr<59:55> == 11111) into a single test.
     */
    bool match = ((ptr_tag + bit55) & 0xf) == 0;
    bool tcma = (desc >> (R_MTEDESC_TCMA_SHIFT + bit55)) & 1;
    return tcma && match;
}

/*
 * For TBI, ideally, we would do nothing.  Proper behaviour on fault is
 * for the tag to be present in the FAR_ELx register.  But for user-only
 * mode, we do not have a TLB with which to implement this, so we must
 * remove the top byte.
 */
static inline uint64_t useronly_clean_ptr(uint64_t ptr)
{
#ifdef CONFIG_USER_ONLY
    /* TBI0 is known to be enabled, while TBI1 is disabled. */
    ptr &= sextract64(ptr, 0, 56);
#endif
    return ptr;
}

static inline uint64_t useronly_maybe_clean_ptr(uint32_t desc, uint64_t ptr)
{
#ifdef CONFIG_USER_ONLY
    int64_t clean_ptr = sextract64(ptr, 0, 56);
    if (tbi_check(desc, clean_ptr < 0)) {
        ptr = clean_ptr;
    }
#endif
    return ptr;
}

/* Values for M-profile PSR.ECI for MVE insns */
enum MVEECIState {
    ECI_NONE = 0, /* No completed beats */
    ECI_A0 = 1, /* Completed: A0 */
    ECI_A0A1 = 2, /* Completed: A0, A1 */
    /* 3 is reserved */
    ECI_A0A1A2 = 4, /* Completed: A0, A1, A2 */
    ECI_A0A1A2B0 = 5, /* Completed: A0, A1, A2, B0 */
    /* All other values reserved */
};

/* Definitions for the PMU registers */
#define PMCRN_MASK  0xf800
#define PMCRN_SHIFT 11
#define PMCRLP  0x80
#define PMCRLC  0x40
#define PMCRDP  0x20
#define PMCRX   0x10
#define PMCRD   0x8
#define PMCRC   0x4
#define PMCRP   0x2
#define PMCRE   0x1
/*
 * Mask of PMCR bits writable by guest (not including WO bits like C, P,
 * which can be written as 1 to trigger behaviour but which stay RAZ).
 */
#define PMCR_WRITABLE_MASK (PMCRLP | PMCRLC | PMCRDP | PMCRX | PMCRD | PMCRE)

#define PMXEVTYPER_P          0x80000000
#define PMXEVTYPER_U          0x40000000
#define PMXEVTYPER_NSK        0x20000000
#define PMXEVTYPER_NSU        0x10000000
#define PMXEVTYPER_NSH        0x08000000
#define PMXEVTYPER_M          0x04000000
#define PMXEVTYPER_MT         0x02000000
#define PMXEVTYPER_EVTCOUNT   0x0000ffff
#define PMXEVTYPER_MASK       (PMXEVTYPER_P | PMXEVTYPER_U | PMXEVTYPER_NSK | \
                               PMXEVTYPER_NSU | PMXEVTYPER_NSH | \
                               PMXEVTYPER_M | PMXEVTYPER_MT | \
                               PMXEVTYPER_EVTCOUNT)

#define PMCCFILTR             0xf8000000
#define PMCCFILTR_M           PMXEVTYPER_M
#define PMCCFILTR_EL0         (PMCCFILTR | PMCCFILTR_M)

static inline uint32_t pmu_num_counters(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);

    return (cpu->isar.reset_pmcr_el0 & PMCRN_MASK) >> PMCRN_SHIFT;
}

/* Bits allowed to be set/cleared for PMCNTEN* and PMINTEN* */
static inline uint64_t pmu_counter_mask(CPUARMState *env)
{
  return (1ULL << 31) | ((1ULL << pmu_num_counters(env)) - 1);
}

#ifdef TARGET_AARCH64
GDBFeature *arm_gen_dynamic_svereg_feature(CPUState *cpu, int base_reg);
int aarch64_gdb_get_sve_reg(CPUState *cs, GByteArray *buf, int reg);
int aarch64_gdb_set_sve_reg(CPUState *cs, uint8_t *buf, int reg);
int aarch64_gdb_get_fpu_reg(CPUState *cs, GByteArray *buf, int reg);
int aarch64_gdb_set_fpu_reg(CPUState *cs, uint8_t *buf, int reg);
int aarch64_gdb_get_pauth_reg(CPUState *cs, GByteArray *buf, int reg);
int aarch64_gdb_set_pauth_reg(CPUState *cs, uint8_t *buf, int reg);
int aarch64_gdb_get_tag_ctl_reg(CPUState *cs, GByteArray *buf, int reg);
int aarch64_gdb_set_tag_ctl_reg(CPUState *cs, uint8_t *buf, int reg);
void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp);
void arm_cpu_sme_finalize(ARMCPU *cpu, Error **errp);
void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp);
void arm_cpu_lpa2_finalize(ARMCPU *cpu, Error **errp);
void aarch64_max_tcg_initfn(Object *obj);
void aarch64_add_pauth_properties(Object *obj);
void aarch64_add_sve_properties(Object *obj);
void aarch64_add_sme_properties(Object *obj);
#endif

/* Read the CONTROL register as the MRS instruction would. */
uint32_t arm_v7m_mrs_control(CPUARMState *env, uint32_t secure);

/*
 * Return a pointer to the location where we currently store the
 * stack pointer for the requested security state and thread mode.
 * This pointer will become invalid if the CPU state is updated
 * such that the stack pointers are switched around (eg changing
 * the SPSEL control bit).
 */
uint32_t *arm_v7m_get_sp_ptr(CPUARMState *env, bool secure,
                             bool threadmode, bool spsel);

bool el_is_in_host(CPUARMState *env, int el);

void aa32_max_features(ARMCPU *cpu);
int exception_target_el(CPUARMState *env);
bool arm_singlestep_active(CPUARMState *env);
bool arm_generate_debug_exceptions(CPUARMState *env);

/**
 * pauth_ptr_mask:
 * @param: parameters defining the MMU setup
 *
 * Return a mask of the address bits that contain the authentication code,
 * given the MMU config defined by @param.
 */
static inline uint64_t pauth_ptr_mask(ARMVAParameters param)
{
    int bot_pac_bit = 64 - param.tsz;
    int top_pac_bit = 64 - 8 * param.tbi;

    return MAKE_64BIT_MASK(bot_pac_bit, top_pac_bit - bot_pac_bit);
}

/* Add the cpreg definitions for debug related system registers */
void define_debug_regs(ARMCPU *cpu);

/* Effective value of MDCR_EL2 */
static inline uint64_t arm_mdcr_el2_eff(CPUARMState *env)
{
    return arm_is_el2_enabled(env) ? env->cp15.mdcr_el2 : 0;
}

/* Powers of 2 for sve_vq_map et al. */
#define SVE_VQ_POW2_MAP                                 \
    ((1 << (1 - 1)) | (1 << (2 - 1)) |                  \
     (1 << (4 - 1)) | (1 << (8 - 1)) | (1 << (16 - 1)))

/*
 * Return true if it is possible to take a fine-grained-trap to EL2.
 */
static inline bool arm_fgt_active(CPUARMState *env, int el)
{
    /*
     * The Arm ARM only requires the "{E2H,TGE} != {1,1}" test for traps
     * that can affect EL0, but it is harmless to do the test also for
     * traps on registers that are only accessible at EL1 because if the test
     * returns true then we can't be executing at EL1 anyway.
     * FGT traps only happen when EL2 is enabled and EL1 is AArch64;
     * traps from AArch32 only happen for the EL0 is AArch32 case.
     */
    return cpu_isar_feature(aa64_fgt, env_archcpu(env)) &&
        el < 2 && arm_is_el2_enabled(env) &&
        arm_el_is_aa64(env, 1) &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE) &&
        (!arm_feature(env, ARM_FEATURE_EL3) || (env->cp15.scr_el3 & SCR_FGTEN));
}

void assert_hflags_rebuild_correctly(CPUARMState *env);

/*
 * Although the ARM implementation of hardware assisted debugging
 * allows for different breakpoints per-core, the current GDB
 * interface treats them as a global pool of registers (which seems to
 * be the case for x86, ppc and s390). As a result we store one copy
 * of registers which is used for all active cores.
 *
 * Write access is serialised by virtue of the GDB protocol which
 * updates things. Read access (i.e. when the values are copied to the
 * vCPU) is also gated by GDB's run control.
 *
 * This is not unreasonable as most of the time debugging kernels you
 * never know which core will eventually execute your function.
 */

typedef struct {
    uint64_t bcr;
    uint64_t bvr;
} HWBreakpoint;

/*
 * The watchpoint registers can cover more area than the requested
 * watchpoint so we need to store the additional information
 * somewhere. We also need to supply a CPUWatchpoint to the GDB stub
 * when the watchpoint is hit.
 */
typedef struct {
    uint64_t wcr;
    uint64_t wvr;
    CPUWatchpoint details;
} HWWatchpoint;

/* Maximum and current break/watch point counts */
extern int max_hw_bps, max_hw_wps;
extern GArray *hw_breakpoints, *hw_watchpoints;

#define cur_hw_wps      (hw_watchpoints->len)
#define cur_hw_bps      (hw_breakpoints->len)
#define get_hw_bp(i)    (&g_array_index(hw_breakpoints, HWBreakpoint, i))
#define get_hw_wp(i)    (&g_array_index(hw_watchpoints, HWWatchpoint, i))

bool find_hw_breakpoint(CPUState *cpu, target_ulong pc);
int insert_hw_breakpoint(target_ulong pc);
int delete_hw_breakpoint(target_ulong pc);

bool check_watchpoint_in_range(int i, target_ulong addr);
CPUWatchpoint *find_hw_watchpoint(CPUState *cpu, target_ulong addr);
int insert_hw_watchpoint(target_ulong addr, target_ulong len, int type);
int delete_hw_watchpoint(target_ulong addr, target_ulong len, int type);

/* Return the current value of the system counter in ticks */
uint64_t gt_get_countervalue(CPUARMState *env);
/*
 * Return the currently applicable offset between the system counter
 * and CNTVCT_EL0 (this will be either 0 or the value of CNTVOFF_EL2).
 */
uint64_t gt_virt_cnt_offset(CPUARMState *env);
#endif
