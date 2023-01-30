/*
 * QEMU ARM CPU -- syndrome functions and types
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

#ifndef TARGET_ARM_SYNDROME_H
#define TARGET_ARM_SYNDROME_H

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
    EC_BXJTRAP                = 0x0a,
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
    EC_ERETTRAP               = 0x1a,
    EC_SMETRAP                = 0x1d,
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

typedef enum {
    SME_ET_AccessTrap,
    SME_ET_Streaming,
    SME_ET_NotStreaming,
    SME_ET_InactiveZA,
} SMEExceptionType;

#define ARM_EL_EC_SHIFT 26
#define ARM_EL_IL_SHIFT 25
#define ARM_EL_ISV_SHIFT 24
#define ARM_EL_IL (1 << ARM_EL_IL_SHIFT)
#define ARM_EL_ISV (1 << ARM_EL_ISV_SHIFT)

static inline uint32_t syn_get_ec(uint32_t syn)
{
    return syn >> ARM_EL_EC_SHIFT;
}

/*
 * Utility functions for constructing various kinds of syndrome value.
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

static inline uint32_t syn_fp_access_trap(int cv, int cond, bool is_16bit,
                                          int coproc)
{
    /* AArch32 FP trap or any AArch64 FP/SIMD trap: TA == 0 */
    return (EC_ADVSIMDFPACCESSTRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL)
        | (cv << 24) | (cond << 20) | coproc;
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

/*
 * eret_op is bits [1:0] of the ERET instruction, so:
 * 0 for ERET, 2 for ERETAA, 3 for ERETAB.
 */
static inline uint32_t syn_erettrap(int eret_op)
{
    return (EC_ERETTRAP << ARM_EL_EC_SHIFT) | ARM_EL_IL | eret_op;
}

static inline uint32_t syn_smetrap(SMEExceptionType etype, bool is_16bit)
{
    return (EC_SMETRAP << ARM_EL_EC_SHIFT)
        | (is_16bit ? 0 : ARM_EL_IL) | etype;
}

static inline uint32_t syn_pactrap(void)
{
    return EC_PACTRAP << ARM_EL_EC_SHIFT;
}

static inline uint32_t syn_btitrap(int btype)
{
    return (EC_BTITRAP << ARM_EL_EC_SHIFT) | btype;
}

static inline uint32_t syn_bxjtrap(int cv, int cond, int rm)
{
    return (EC_BXJTRAP << ARM_EL_EC_SHIFT) | ARM_EL_IL |
        (cv << 24) | (cond << 20) | rm;
}

static inline uint32_t syn_insn_abort(int same_el, int ea, int s1ptw, int fsc)
{
    return (EC_INSNABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (ea << 9) | (s1ptw << 7) | fsc;
}

static inline uint32_t syn_data_abort_no_iss(int same_el, int fnv,
                                             int ea, int cm, int s1ptw,
                                             int wnr, int fsc)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
           | ARM_EL_IL
           | (fnv << 10) | (ea << 9) | (cm << 8) | (s1ptw << 7)
           | (wnr << 6) | fsc;
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

static inline uint32_t syn_illegalstate(void)
{
    return (EC_ILLEGALSTATE << ARM_EL_EC_SHIFT) | ARM_EL_IL;
}

static inline uint32_t syn_pcalignment(void)
{
    return (EC_PCALIGNMENT << ARM_EL_EC_SHIFT) | ARM_EL_IL;
}

static inline uint32_t syn_serror(uint32_t extra)
{
    return (EC_SERROR << ARM_EL_EC_SHIFT) | ARM_EL_IL | extra;
}

#endif /* TARGET_ARM_SYNDROME_H */
