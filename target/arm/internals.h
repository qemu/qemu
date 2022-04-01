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

#include "hw/registerfields.h"
#include "tcg/tcg-gvec-desc.h"
#include "syndrome.h"

/* register banks for CPU modes */
#define BANK_USRSYS 0
#define BANK_SVC    1
#define BANK_ABT    2
#define BANK_UND    3
#define BANK_IRQ    4
#define BANK_FIQ    5
#define BANK_HYP    6
#define BANK_MON    7

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

/* Scale factor for generic timers, ie number of ns per tick.
 * This gives a 62.5MHz timer.
 */
#define GTIMER_SCALE 16

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
 * raise_exception: Raise the specified exception.
 * Raise a guest exception with the specified value, syndrome register
 * and target exception level. This should be called from helper functions,
 * and never returns because we will longjump back up to the CPU main loop.
 */
void QEMU_NORETURN raise_exception(CPUARMState *env, uint32_t excp,
                                   uint32_t syndrome, uint32_t target_el);

/*
 * Similarly, but also use unwinding to restore cpu state.
 */
void QEMU_NORETURN raise_exception_ra(CPUARMState *env, uint32_t excp,
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

void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu);
void arm_translate_init(void);

#ifdef CONFIG_TCG
void arm_cpu_synchronize_from_tb(CPUState *cs, const TranslationBlock *tb);
#endif /* CONFIG_TCG */

/**
 * aarch64_sve_zcr_get_valid_len:
 * @cpu: cpu context
 * @start_len: maximum len to consider
 *
 * Return the maximum supported sve vector length <= @start_len.
 * Note that both @start_len and the return value are in units
 * of ZCR_ELx.LEN, so the vector bit length is (x + 1) * 128.
 */
uint32_t aarch64_sve_zcr_get_valid_len(ARMCPU *cpu, uint32_t start_len);

enum arm_fprounding {
    FPROUNDING_TIEEVEN,
    FPROUNDING_POSINF,
    FPROUNDING_NEGINF,
    FPROUNDING_ZERO,
    FPROUNDING_TIEAWAY,
    FPROUNDING_ODD
};

int arm_rmode_to_sf(int rmode);

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
    TCR *tcr = &env->cp15.tcr_el[arm_is_secure(env) ? 3 : 1];
    return arm_el_is_aa64(env, 1) ||
           (arm_feature(env, ARM_FEATURE_LPAE) && (tcr->raw_tcr & TTBCR_EAE));
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
    ARMFault_Lockdown,
    ARMFault_Exclusive,
    ARMFault_ICacheMaint,
    ARMFault_QEMU_NSCExec, /* v8M: NS executing in S&NSC memory */
    ARMFault_QEMU_SFault, /* v8M: SecureFault INVTRAN, INVEP or AUVIOL */
} ARMFaultType;

/**
 * ARMMMUFaultInfo: Information describing an ARM MMU Fault
 * @type: Type of fault
 * @level: Table walk level (for translation, access flag and permission faults)
 * @domain: Domain of the fault address (for non-LPAE CPUs only)
 * @s2addr: Address that caused a fault at stage 2
 * @stage2: True if we faulted at stage 2
 * @s1ptw: True if we faulted at stage 2 while doing a stage 1 page-table walk
 * @s1ns: True if we faulted on a non-secure IPA while in secure state
 * @ea: True if we should set the EA (external abort type) bit in syndrome
 */
typedef struct ARMMMUFaultInfo ARMMMUFaultInfo;
struct ARMMMUFaultInfo {
    ARMFaultType type;
    target_ulong s2addr;
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
    case ARMFault_Lockdown:
        fsc = 0x34;
        break;
    case ARMFault_Exclusive:
        fsc = 0x35;
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

int arm_mmu_idx_to_el(ARMMMUIdx mmu_idx);

/*
 * Return the MMU index for a v7M CPU with all relevant information
 * manually specified.
 */
ARMMMUIdx arm_v7m_mmu_idx_all(CPUARMState *env,
                              bool secstate, bool priv, bool negpri);

/*
 * Return the MMU index for a v7M CPU in the specified security and
 * privilege state.
 */
ARMMMUIdx arm_v7m_mmu_idx_for_secstate_and_priv(CPUARMState *env,
                                                bool secstate, bool priv);

/* Return the MMU index for a v7M CPU in the specified security state */
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate);

/* Return true if the stage 1 translation regime is using LPAE format page
 * tables */
bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx);

/* Raise a data fault alignment exception for the specified virtual address */
void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr) QEMU_NORETURN;

/* arm_cpu_do_transaction_failed: handle a memory system error response
 * (eg "no device/memory present at address") by raising an external abort
 * exception
 */
void arm_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr addr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr);

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
    case ARMMMUIdx_Stage1_SE0:
    case ARMMMUIdx_Stage1_SE1:
    case ARMMMUIdx_Stage1_SE1_PAN:
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
    case ARMMMUIdx_SE10_0:
    case ARMMMUIdx_SE10_1:
    case ARMMMUIdx_SE10_1_PAN:
    case ARMMMUIdx_SE20_0:
    case ARMMMUIdx_SE20_2:
    case ARMMMUIdx_SE20_2_PAN:
        return true;
    default:
        return false;
    }
}

/* Return true if this address translation regime is secure */
static inline bool regime_is_secure(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_Stage1_E1:
    case ARMMMUIdx_Stage1_E1_PAN:
    case ARMMMUIdx_E2:
    case ARMMMUIdx_Stage2:
    case ARMMMUIdx_MPrivNegPri:
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MPriv:
    case ARMMMUIdx_MUser:
        return false;
    case ARMMMUIdx_SE3:
    case ARMMMUIdx_SE10_0:
    case ARMMMUIdx_SE10_1:
    case ARMMMUIdx_SE10_1_PAN:
    case ARMMMUIdx_SE20_0:
    case ARMMMUIdx_SE20_2:
    case ARMMMUIdx_SE20_2_PAN:
    case ARMMMUIdx_Stage1_SE0:
    case ARMMMUIdx_Stage1_SE1:
    case ARMMMUIdx_Stage1_SE1_PAN:
    case ARMMMUIdx_SE2:
    case ARMMMUIdx_Stage2_S:
    case ARMMMUIdx_MSPrivNegPri:
    case ARMMMUIdx_MSUserNegPri:
    case ARMMMUIdx_MSPriv:
    case ARMMMUIdx_MSUser:
        return true;
    default:
        g_assert_not_reached();
    }
}

static inline bool regime_is_pan(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_Stage1_E1_PAN:
    case ARMMMUIdx_Stage1_SE1_PAN:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_E20_2_PAN:
    case ARMMMUIdx_SE10_1_PAN:
    case ARMMMUIdx_SE20_2_PAN:
        return true;
    default:
        return false;
    }
}

/* Return the exception level which controls this address translation regime */
static inline uint32_t regime_el(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_SE20_0:
    case ARMMMUIdx_SE20_2:
    case ARMMMUIdx_SE20_2_PAN:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
    case ARMMMUIdx_Stage2:
    case ARMMMUIdx_Stage2_S:
    case ARMMMUIdx_SE2:
    case ARMMMUIdx_E2:
        return 2;
    case ARMMMUIdx_SE3:
        return 3;
    case ARMMMUIdx_SE10_0:
    case ARMMMUIdx_Stage1_SE0:
        return arm_el_is_aa64(env, 3) ? 1 : 3;
    case ARMMMUIdx_SE10_1:
    case ARMMMUIdx_SE10_1_PAN:
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_Stage1_E1:
    case ARMMMUIdx_Stage1_E1_PAN:
    case ARMMMUIdx_Stage1_SE1:
    case ARMMMUIdx_Stage1_SE1_PAN:
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
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

/* Return the TCR controlling this translation regime */
static inline TCR *regime_tcr(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    if (mmu_idx == ARMMMUIdx_Stage2) {
        return &env->cp15.vtcr_el2;
    }
    if (mmu_idx == ARMMMUIdx_Stage2_S) {
        /*
         * Note: Secure stage 2 nominally shares fields from VTCR_EL2, but
         * those are not currently used by QEMU, so just return VSTCR_EL2.
         */
        return &env->cp15.vstcr_el2;
    }
    return &env->cp15.tcr_el[regime_el(env, mmu_idx)];
}

/* Return the FSR value for a debug exception (watchpoint, hardware
 * breakpoint or BKPT insn) targeting the specified exception level.
 */
static inline uint32_t arm_debug_exception_fsr(CPUARMState *env)
{
    ARMMMUFaultInfo fi = { .type = ARMFault_Debug };
    int target_el = arm_debug_target_el(env);
    bool using_lpae = false;

    if (target_el == 2 || arm_el_is_aa64(env, target_el)) {
        using_lpae = true;
    } else {
        if (arm_feature(env, ARM_FEATURE_LPAE) &&
            (env->cp15.tcr_el[target_el].raw_tcr & TTBCR_EAE)) {
            using_lpae = true;
        }
    }

    if (using_lpae) {
        return arm_fi_to_lfsc(&fi);
    } else {
        return arm_fi_to_sfsc(&fi);
    }
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
 * Must be called with the iothread lock held.
 */
void arm_cpu_update_virq(ARMCPU *cpu);

/**
 * arm_cpu_update_vfiq: Update CPU_INTERRUPT_VFIQ bit in cs->interrupt_request
 *
 * Update the CPU_INTERRUPT_VFIQ bit in cs->interrupt_request, following
 * a change to either the input VFIQ line from the GIC or the HCR_EL2.VF bit.
 * Must be called with the iothread lock held.
 */
void arm_cpu_update_vfiq(ARMCPU *cpu);

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
static inline ARMMMUIdx arm_stage1_mmu_idx(CPUARMState *env)
{
    return ARMMMUIdx_Stage1_E0;
}
#else
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
    case ARMMMUIdx_Stage1_SE0:
    case ARMMMUIdx_Stage1_SE1:
    case ARMMMUIdx_Stage1_SE1_PAN:
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

    return valid;
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
    bool using16k   : 1;
    bool using64k   : 1;
    bool tsz_oob    : 1;  /* tsz has been clamped to legal range */
    bool ds         : 1;
} ARMVAParameters;

ARMVAParameters aa64_va_parameters(CPUARMState *env, uint64_t va,
                                   ARMMMUIdx mmu_idx, bool data);

static inline int exception_target_el(CPUARMState *env)
{
    int target_el = MAX(1, arm_current_el(env));

    /*
     * No such thing as secure EL1 if EL3 is aarch32,
     * so update the target EL to EL3 in this case.
     */
    if (arm_is_secure(env) && !arm_el_is_aa64(env, 3) && target_el == 1) {
        target_el = 3;
    }

    return target_el;
}

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
                         V8M_SAttributes *sattrs);

bool pmsav8_mpu_lookup(CPUARMState *env, uint32_t address,
                       MMUAccessType access_type, ARMMMUIdx mmu_idx,
                       hwaddr *phys_ptr, MemTxAttrs *txattrs,
                       int *prot, bool *is_subpage,
                       ARMMMUFaultInfo *fi, uint32_t *mregion);

/* Cacheability and shareability attributes for a memory access */
typedef struct ARMCacheAttrs {
    unsigned int attrs:8; /* as in the MAIR register encoding */
    unsigned int shareability:2; /* as in the SH field of the VMSAv8-64 PTEs */
} ARMCacheAttrs;

bool get_phys_addr(CPUARMState *env, target_ulong address,
                   MMUAccessType access_type, ARMMMUIdx mmu_idx,
                   hwaddr *phys_ptr, MemTxAttrs *attrs, int *prot,
                   target_ulong *page_size,
                   ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs)
    __attribute__((nonnull));

void arm_log_exception(CPUState *cs);

#endif /* !CONFIG_USER_ONLY */

/*
 * The log2 of the words in the tag block, for GMID_EL1.BS.
 * The is the maximum, 256 bytes, which manipulates 64-bits of tags.
 */
#define GMID_EL1_BS  6

/* We associate one allocation tag per 16 bytes, the minimum.  */
#define LOG2_TAG_GRANULE 4
#define TAG_GRANULE      (1 << LOG2_TAG_GRANULE)

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
FIELD(MTEDESC, SIZEM1, 9, SIMD_DATA_BITS - 9)  /* size - 1 */

bool mte_probe(CPUARMState *env, uint32_t desc, uint64_t ptr);
uint64_t mte_check(CPUARMState *env, uint32_t desc, uint64_t ptr, uintptr_t ra);

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
#define PMCRLC  0x40
#define PMCRDP  0x20
#define PMCRX   0x10
#define PMCRD   0x8
#define PMCRC   0x4
#define PMCRP   0x2
#define PMCRE   0x1
/*
 * Mask of PMCR bits writeable by guest (not including WO bits like C, P,
 * which can be written as 1 to trigger behaviour but which stay RAZ).
 */
#define PMCR_WRITEABLE_MASK (PMCRLC | PMCRDP | PMCRX | PMCRD | PMCRE)

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
  return (env->cp15.c9_pmcr & PMCRN_MASK) >> PMCRN_SHIFT;
}

/* Bits allowed to be set/cleared for PMCNTEN* and PMINTEN* */
static inline uint64_t pmu_counter_mask(CPUARMState *env)
{
  return (1 << 31) | ((1 << pmu_num_counters(env)) - 1);
}

#ifdef TARGET_AARCH64
int arm_gdb_get_svereg(CPUARMState *env, GByteArray *buf, int reg);
int arm_gdb_set_svereg(CPUARMState *env, uint8_t *buf, int reg);
int aarch64_fpu_gdb_get_reg(CPUARMState *env, GByteArray *buf, int reg);
int aarch64_fpu_gdb_set_reg(CPUARMState *env, uint8_t *buf, int reg);
#endif

#endif
