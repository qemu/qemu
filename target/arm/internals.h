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
static inline unsigned int arm_pamax(ARMCPU *cpu)
{
    static const unsigned int pamax_map[] = {
        [0] = 32,
        [1] = 36,
        [2] = 40,
        [3] = 42,
        [4] = 44,
        [5] = 48,
    };
    unsigned int parange =
        FIELD_EX64(cpu->isar.id_aa64mmfr0, ID_AA64MMFR0, PARANGE);

    /* id_aa64mmfr0 is a read-only register so values outside of the
     * supported mappings can be considered an implementation error.  */
    assert(parange < ARRAY_SIZE(pamax_map));
    return pamax_map[parange];
}

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

/* Valid Syndrome Register EC field values */
enum arm_exception_class {
    EC_UNCATEGORIZED          = 0x00,
    EC_WFX_TRAP               = 0x01,
    EC_CP15RTTRAP             = 0x03,
    EC_CP15RRTTRAP            = 0x04,
    EC_CP14RTTRAP             = 0x05,
    EC_CP14DTTRAP             = 0x06,
    EC_ADVSIMDFPACCESSTRAP    = 0x07,
    EC_FPIDTRAP               = 0x08,
    EC_PACTRAP                = 0x09,
    EC_CP14RRTTRAP            = 0x0c,
    EC_BTITRAP                = 0x0d,
    EC_ILLEGALSTATE           = 0x0e,
    EC_AA32_SVC               = 0x11,
    EC_AA32_HVC               = 0x12,
    EC_AA32_SMC               = 0x13,
    EC_AA64_SVC               = 0x15,
    EC_AA64_HVC               = 0x16,
    EC_AA64_SMC               = 0x17,
    EC_SYSTEMREGISTERTRAP     = 0x18,
    EC_SVEACCESSTRAP          = 0x19,
    EC_INSNABORT              = 0x20,
    EC_INSNABORT_SAME_EL      = 0x21,
    EC_PCALIGNMENT            = 0x22,
    EC_DATAABORT              = 0x24,
    EC_DATAABORT_SAME_EL      = 0x25,
    EC_SPALIGNMENT            = 0x26,
    EC_AA32_FPTRAP            = 0x28,
    EC_AA64_FPTRAP            = 0x2c,
    EC_SERROR                 = 0x2f,
    EC_BREAKPOINT             = 0x30,
    EC_BREAKPOINT_SAME_EL     = 0x31,
    EC_SOFTWARESTEP           = 0x32,
    EC_SOFTWARESTEP_SAME_EL   = 0x33,
    EC_WATCHPOINT             = 0x34,
    EC_WATCHPOINT_SAME_EL     = 0x35,
    EC_AA32_BKPT              = 0x38,
    EC_VECTORCATCH            = 0x3a,
    EC_AA64_BKPT              = 0x3c,
};

#define ARM_EL_EC_SHIFT 26
#define ARM_EL_IL_SHIFT 25
#define ARM_EL_ISV_SHIFT 24
#define ARM_EL_IL (1 << ARM_EL_IL_SHIFT)
#define ARM_EL_ISV (1 << ARM_EL_ISV_SHIFT)

static inline uint32_t syn_get_ec(uint32_t syn)
{
    return syn >> ARM_EL_EC_SHIFT;
}

/* Utility functions for constructing various kinds of syndrome value.
 * Note that in general we follow the AArch64 syndrome values; in a
 * few cases the value in HSR for exceptions taken to AArch32 Hyp
 * mode differs slightly, and we fix this up when populating HSR in
 * arm_cpu_do_interrupt_aarch32_hyp().
 * The exception is FP/SIMD access traps -- these report extra information
 * when taking an exception to AArch32. For those we include the extra coproc
 * and TA fields, and mask them out when taking the exception to AArch64.
 */
static inline uint32_t syn_uncategorized(void)
{
    return (EC_UNCATEGORIZED << ARM_EL_EC_SHIFT) | ARM_EL_IL;
}

static inline uint32_t syn_aa64_svc(uint32_t imm16)
{
    return (EC_AA64_SVC << ARM_EL_EC_SHIFT) | ARM_EL_IL | (imm16 & 0xffff);
}

static inline uint32_t syn_aa64_hvc(uint32_t imm16)
{
    return (EC_AA64_HVC << ARM_EL_EC_SHIFT) | ARM_EL_IL | (imm16 & 0xffff);
}

static inline uint32_t syn_aa64_smc(uint32_t imm16)
{
    return (EC_AA64_SMC << ARM_EL_EC_SHIFT) | ARM_EL_IL | (imm16 & 0xffff);
}

static inline uint32_t syn_aa32_svc(uint32_t imm16, bool is_16bit)
{
    return (EC_AA32_SVC << ARM_EL_EC_SHIFT) | (imm16 & 0xffff)
        | (is_16bit ? 0 : ARM_EL_IL);
}

static inline uint32_t syn_aa32_hvc(uint32_t imm16)
{
    return (EC_AA32_HVC << ARM_EL_EC_SHIFT) | ARM_EL_IL | (imm16 & 0xffff);
}

static inline uint32_t syn_aa32_smc(void)
{
    return (EC_AA32_SMC << ARM_EL_EC_SHIFT) | ARM_EL_IL;
}

static inline uint32_t syn_aa64_bkpt(uint32_t imm16)
{
    return (EC_AA64_BKPT << ARM_EL_EC_SHIFT) | ARM_EL_IL | (imm16 & 0xffff);
}

static inline uint32_t syn_aa32_bkpt(uint32_t imm16, bool is_16bit)
{
    return (EC_AA32_BKPT << ARM_EL_EC_SHIFT) | (imm16 & 0xffff)
        | (is_16bit ? 0 : ARM_EL_IL);
}

static inline uint32_t syn_aa64_sysregtrap(int op0, int op1, int op2,
                                           int crn, int crm, int rt,
                                           int isread)
{
    return (EC_SYSTEMREGISTERTRAP << ARM_EL_EC_SHIFT) | ARM_EL_IL
        | (op0 << 20) | (op2 << 17) | (op1 << 14) | (crn << 10) | (rt << 5)
        | (crm << 1) | isread;
}

static inline uint32_t syn_cp14_rt_trap(int cv, int cond, int opc1, int opc2,
                                        int crn, int crm, int rt, int isread,
                                        bool is_16bit)
{
    return (EC_CP14RTTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc2 << 17) | (opc1 << 14)
        | (crn << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp15_rt_trap(int cv, int cond, int opc1, int opc2,
                                        int crn, int crm, int rt, int isread,
                                        bool is_16bit)
{
    return (EC_CP15RTTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc2 << 17) | (opc1 << 14)
        | (crn << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp14_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_16bit)
{
    return (EC_CP14RRTTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc1 << 16)
        | (rt2 << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp15_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_16bit)
{
    return (EC_CP15RRTTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc1 << 16)
        | (rt2 << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_fp_access_trap(int cv, int cond, bool is_16bit)
{
    /* AArch32 FP trap or any AArch64 FP/SIMD trap: TA == 0 coproc == 0xa */
    return (EC_ADVSIMDFPACCESSTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | 0xa;
}

static inline uint32_t syn_simd_access_trap(int cv, int cond, bool is_16bit)
{
    /* AArch32 SIMD trap: TA == 1 coproc == 0 */
    return (EC_ADVSIMDFPACCESSTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (1 << 5);
}

static inline uint32_t syn_sve_access_trap(void)
{
    return EC_SVEACCESSTRAP << ARM_EL_EC_SHIFT;
}

static inline uint32_t syn_pactrap(void)
{
    return EC_PACTRAP << ARM_EL_EC_SHIFT;
}

static inline uint32_t syn_btitrap(int btype)
{
    return (EC_BTITRAP << ARM_EL_EC_SHIFT) | btype;
}

static inline uint32_t syn_insn_abort(int same_el, int ea, int s1ptw, int fsc)
{
    return (EC_INSNABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (ea << 9) | (s1ptw << 7) | fsc;
}

static inline uint32_t syn_data_abort_no_iss(int same_el,
                                             int ea, int cm, int s1ptw,
                                             int wnr, int fsc)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
           | ARM_EL_IL
           | (ea << 9) | (cm << 8) | (s1ptw << 7) | (wnr << 6) | fsc;
}

static inline uint32_t syn_data_abort_with_iss(int same_el,
                                               int sas, int sse, int srt,
                                               int sf, int ar,
                                               int ea, int cm, int s1ptw,
                                               int wnr, int fsc,
                                               bool is_16bit)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
           | (is_16bit ? 0 : ARM_EL_IL)
           | ARM_EL_ISV | (sas << 22) | (sse << 21) | (srt << 16)
           | (sf << 15) | (ar << 14)
           | (ea << 9) | (cm << 8) | (s1ptw << 7) | (wnr << 6) | fsc;
}

static inline uint32_t syn_swstep(int same_el, int isv, int ex)
{
    return (EC_SOFTWARESTEP << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (isv << 24) | (ex << 6) | 0x22;
}

static inline uint32_t syn_watchpoint(int same_el, int cm, int wnr)
{
    return (EC_WATCHPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (cm << 8) | (wnr << 6) | 0x22;
}

static inline uint32_t syn_breakpoint(int same_el)
{
    return (EC_BREAKPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | 0x22;
}

static inline uint32_t syn_wfx(int cv, int cond, int ti, bool is_16bit)
{
    return (EC_WFX_TRAP << ARM_EL_EC_SHIFT) |
           (is_16bit ? 0 : (1 << ARM_EL_IL_SHIFT)) |
           (cv << 24) | (cond << 20) | ti;
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
        fsc = fi->level & 3;
        break;
    case ARMFault_AccessFlag:
        fsc = (fi->level & 3) | (0x2 << 2);
        break;
    case ARMFault_Permission:
        fsc = (fi->level & 3) | (0x3 << 2);
        break;
    case ARMFault_Translation:
        fsc = (fi->level & 3) | (0x1 << 2);
        break;
    case ARMFault_SyncExternal:
        fsc = 0x10 | (fi->ea << 12);
        break;
    case ARMFault_SyncExternalOnWalk:
        fsc = (fi->level & 3) | (0x5 << 2) | (fi->ea << 12);
        break;
    case ARMFault_SyncParity:
        fsc = 0x18;
        break;
    case ARMFault_SyncParityOnWalk:
        fsc = (fi->level & 3) | (0x7 << 2);
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

bool arm_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);

/* Return true if the stage 1 translation regime is using LPAE format page
 * tables */
bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx);

/* Raise a data fault alignment exception for the specified virtual address */
void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr);

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

/* Return true if this address translation regime is secure */
static inline bool regime_is_secure(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_S12NSE0:
    case ARMMMUIdx_S12NSE1:
    case ARMMMUIdx_S1NSE0:
    case ARMMMUIdx_S1NSE1:
    case ARMMMUIdx_S1E2:
    case ARMMMUIdx_S2NS:
    case ARMMMUIdx_MPrivNegPri:
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MPriv:
    case ARMMMUIdx_MUser:
        return false;
    case ARMMMUIdx_S1E3:
    case ARMMMUIdx_S1SE0:
    case ARMMMUIdx_S1SE1:
    case ARMMMUIdx_MSPrivNegPri:
    case ARMMMUIdx_MSUserNegPri:
    case ARMMMUIdx_MSPriv:
    case ARMMMUIdx_MSUser:
        return true;
    default:
        g_assert_not_reached();
    }
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

/* Note make_memop_idx reserves 4 bits for mmu_idx, and MO_BSWAP is bit 3.
 * Thus a TCGMemOpIdx, without any MO_ALIGN bits, fits in 8 bits.
 */
#define MEMOPIDX_SHIFT  8

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
    return ARMMMUIdx_S1NSE0;
}
#else
ARMMMUIdx arm_stage1_mmu_idx(CPUARMState *env);
#endif

/*
 * Parameters of a given virtual address, as extracted from the
 * translation control register (TCR) for a given regime.
 */
typedef struct ARMVAParameters {
    unsigned tsz    : 8;
    unsigned select : 1;
    bool tbi        : 1;
    bool tbid       : 1;
    bool epd        : 1;
    bool hpd        : 1;
    bool using16k   : 1;
    bool using64k   : 1;
} ARMVAParameters;

ARMVAParameters aa64_va_parameters_both(CPUARMState *env, uint64_t va,
                                        ARMMMUIdx mmu_idx);
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
                   ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs);

void arm_log_exception(int idx);

#endif /* !CONFIG_USER_ONLY */

#endif
