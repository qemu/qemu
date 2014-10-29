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
 * between different source files within target-arm/ but which are
 * private to it and not required by the rest of QEMU.
 */

#ifndef TARGET_ARM_INTERNALS_H
#define TARGET_ARM_INTERNALS_H

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
        || excp == EXCP_STREX;
}

/* Exception names for debug logging; note that not all of these
 * precisely correspond to architectural exceptions.
 */
static const char * const excnames[] = {
    [EXCP_UDEF] = "Undefined Instruction",
    [EXCP_SWI] = "SVC",
    [EXCP_PREFETCH_ABORT] = "Prefetch Abort",
    [EXCP_DATA_ABORT] = "Data Abort",
    [EXCP_IRQ] = "IRQ",
    [EXCP_FIQ] = "FIQ",
    [EXCP_BKPT] = "Breakpoint",
    [EXCP_EXCEPTION_EXIT] = "QEMU v7M exception exit",
    [EXCP_KERNEL_TRAP] = "QEMU intercept of kernel commpage",
    [EXCP_STREX] = "QEMU intercept of STREX",
    [EXCP_HVC] = "Hypervisor Call",
    [EXCP_HYP_TRAP] = "Hypervisor Trap",
    [EXCP_SMC] = "Secure Monitor Call",
    [EXCP_VIRQ] = "Virtual IRQ",
    [EXCP_VFIQ] = "Virtual FIQ",
};

static inline void arm_log_exception(int idx)
{
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        const char *exc = NULL;

        if (idx >= 0 && idx < ARRAY_SIZE(excnames)) {
            exc = excnames[idx];
        }
        if (!exc) {
            exc = "unknown";
        }
        qemu_log_mask(CPU_LOG_INT, "Taking exception %d [%s]\n", idx, exc);
    }
}

/* Scale factor for generic timers, ie number of ns per tick.
 * This gives a 62.5MHz timer.
 */
#define GTIMER_SCALE 16

/*
 * For AArch64, map a given EL to an index in the banked_spsr array.
 */
static inline unsigned int aarch64_banked_spsr_index(unsigned int el)
{
    static const unsigned int map[4] = {
        [1] = 0, /* EL1.  */
        [2] = 6, /* EL2.  */
        [3] = 7, /* EL3.  */
    };
    assert(el >= 1 && el <= 3);
    return map[el];
}

int bank_number(int mode);
void switch_mode(CPUARMState *, int);
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

/* Return true if extended addresses are enabled.
 * This is always the case if our translation regime is 64 bit,
 * but depends on TTBCR.EAE for 32 bit.
 */
static inline bool extended_addresses_enabled(CPUARMState *env)
{
    return arm_el_is_aa64(env, 1)
        || ((arm_feature(env, ARM_FEATURE_LPAE)
             && (env->cp15.c2_control & TTBCR_EAE)));
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
    EC_CP14RRTTRAP            = 0x0c,
    EC_ILLEGALSTATE           = 0x0e,
    EC_AA32_SVC               = 0x11,
    EC_AA32_HVC               = 0x12,
    EC_AA32_SMC               = 0x13,
    EC_AA64_SVC               = 0x15,
    EC_AA64_HVC               = 0x16,
    EC_AA64_SMC               = 0x17,
    EC_SYSTEMREGISTERTRAP     = 0x18,
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
#define ARM_EL_IL (1 << ARM_EL_IL_SHIFT)

/* Utility functions for constructing various kinds of syndrome value.
 * Note that in general we follow the AArch64 syndrome values; in a
 * few cases the value in HSR for exceptions taken to AArch32 Hyp
 * mode differs slightly, so if we ever implemented Hyp mode then the
 * syndrome value would need some massaging on exception entry.
 * (One example of this is that AArch64 defaults to IL bit set for
 * exceptions which don't specifically indicate information about the
 * trapping instruction, whereas AArch32 defaults to IL bit clear.)
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

static inline uint32_t syn_aa32_svc(uint32_t imm16, bool is_thumb)
{
    return (EC_AA32_SVC << ARM_EL_EC_SHIFT) | (imm16 & 0xffff)
        | (is_thumb ? 0 : ARM_EL_IL);
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

static inline uint32_t syn_aa32_bkpt(uint32_t imm16, bool is_thumb)
{
    return (EC_AA32_BKPT << ARM_EL_EC_SHIFT) | (imm16 & 0xffff)
        | (is_thumb ? 0 : ARM_EL_IL);
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
                                        bool is_thumb)
{
    return (EC_CP14RTTRAP << ARM_EL_EC_SHIFT)
        | (is_thumb ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc2 << 17) | (opc1 << 14)
        | (crn << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp15_rt_trap(int cv, int cond, int opc1, int opc2,
                                        int crn, int crm, int rt, int isread,
                                        bool is_thumb)
{
    return (EC_CP15RTTRAP << ARM_EL_EC_SHIFT)
        | (is_thumb ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc2 << 17) | (opc1 << 14)
        | (crn << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp14_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_thumb)
{
    return (EC_CP14RRTTRAP << ARM_EL_EC_SHIFT)
        | (is_thumb ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc1 << 16)
        | (rt2 << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_cp15_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_thumb)
{
    return (EC_CP15RRTTRAP << ARM_EL_EC_SHIFT)
        | (is_thumb ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | (opc1 << 16)
        | (rt2 << 10) | (rt << 5) | (crm << 1) | isread;
}

static inline uint32_t syn_fp_access_trap(int cv, int cond, bool is_thumb)
{
    return (EC_ADVSIMDFPACCESSTRAP << ARM_EL_EC_SHIFT)
        | (is_thumb ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20);
}

static inline uint32_t syn_insn_abort(int same_el, int ea, int s1ptw, int fsc)
{
    return (EC_INSNABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | (ea << 9) | (s1ptw << 7) | fsc;
}

static inline uint32_t syn_data_abort(int same_el, int ea, int cm, int s1ptw,
                                      int wnr, int fsc)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | (ea << 9) | (cm << 8) | (s1ptw << 7) | (wnr << 6) | fsc;
}

static inline uint32_t syn_swstep(int same_el, int isv, int ex)
{
    return (EC_SOFTWARESTEP << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | (isv << 24) | (ex << 6) | 0x22;
}

static inline uint32_t syn_watchpoint(int same_el, int cm, int wnr)
{
    return (EC_WATCHPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | (cm << 8) | (wnr << 6) | 0x22;
}

static inline uint32_t syn_breakpoint(int same_el)
{
    return (EC_BREAKPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | 0x22;
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

/* Callback function for when a watchpoint or breakpoint triggers. */
void arm_debug_excp_handler(CPUState *cs);

#ifdef CONFIG_USER_ONLY
static inline bool arm_is_psci_call(ARMCPU *cpu, int excp_type)
{
    return false;
}
#else
/* Return true if the r0/x0 value indicates that this SMC/HVC is a PSCI call. */
bool arm_is_psci_call(ARMCPU *cpu, int excp_type);
/* Actually handle a PSCI call */
void arm_handle_psci_call(ARMCPU *cpu);
#endif

#endif
