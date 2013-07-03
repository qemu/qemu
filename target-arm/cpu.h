/*
 * ARM virtual CPU header
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_ARM_H
#define CPU_ARM_H

#define TARGET_LONG_BITS 32

#define ELF_MACHINE	EM_ARM

#define CPUArchState struct CPUARMState

#include "config.h"
#include "qemu-common.h"
#include "exec/cpu-defs.h"

#include "fpu/softfloat.h"

#define TARGET_HAS_ICE 1

#define EXCP_UDEF            1   /* undefined instruction */
#define EXCP_SWI             2   /* software interrupt */
#define EXCP_PREFETCH_ABORT  3
#define EXCP_DATA_ABORT      4
#define EXCP_IRQ             5
#define EXCP_FIQ             6
#define EXCP_BKPT            7
#define EXCP_EXCEPTION_EXIT  8   /* Return from v7M exception.  */
#define EXCP_KERNEL_TRAP     9   /* Jumped to kernel code page.  */
#define EXCP_STREX          10

#define ARMV7M_EXCP_RESET   1
#define ARMV7M_EXCP_NMI     2
#define ARMV7M_EXCP_HARD    3
#define ARMV7M_EXCP_MEM     4
#define ARMV7M_EXCP_BUS     5
#define ARMV7M_EXCP_USAGE   6
#define ARMV7M_EXCP_SVC     11
#define ARMV7M_EXCP_DEBUG   12
#define ARMV7M_EXCP_PENDSV  14
#define ARMV7M_EXCP_SYSTICK 15

/* ARM-specific interrupt pending bits.  */
#define CPU_INTERRUPT_FIQ   CPU_INTERRUPT_TGT_EXT_1


typedef void ARMWriteCPFunc(void *opaque, int cp_info,
                            int srcreg, int operand, uint32_t value);
typedef uint32_t ARMReadCPFunc(void *opaque, int cp_info,
                               int dstreg, int operand);

struct arm_boot_info;

#define NB_MMU_MODES 2

/* We currently assume float and double are IEEE single and double
   precision respectively.
   Doing runtime conversions is tricky because VFP registers may contain
   integer values (eg. as the result of a FTOSI instruction).
   s<2n> maps to the least significant half of d<n>
   s<2n+1> maps to the most significant half of d<n>
 */

typedef struct CPUARMState {
    /* Regs for current mode.  */
    uint32_t regs[16];
    /* Frequently accessed CPSR bits are stored separately for efficiency.
       This contains all the other bits.  Use cpsr_{read,write} to access
       the whole CPSR.  */
    uint32_t uncached_cpsr;
    uint32_t spsr;

    /* Banked registers.  */
    uint32_t banked_spsr[6];
    uint32_t banked_r13[6];
    uint32_t banked_r14[6];

    /* These hold r8-r12.  */
    uint32_t usr_regs[5];
    uint32_t fiq_regs[5];

    /* cpsr flag cache for faster execution */
    uint32_t CF; /* 0 or 1 */
    uint32_t VF; /* V is the bit 31. All other bits are undefined */
    uint32_t NF; /* N is bit 31. All other bits are undefined.  */
    uint32_t ZF; /* Z set if zero.  */
    uint32_t QF; /* 0 or 1 */
    uint32_t GE; /* cpsr[19:16] */
    uint32_t thumb; /* cpsr[5]. 0 = arm mode, 1 = thumb mode. */
    uint32_t condexec_bits; /* IT bits.  cpsr[15:10,26:25].  */

    /* System control coprocessor (cp15) */
    struct {
        uint32_t c0_cpuid;
        uint32_t c0_cssel; /* Cache size selection.  */
        uint32_t c1_sys; /* System control register.  */
        uint32_t c1_coproc; /* Coprocessor access register.  */
        uint32_t c1_xscaleauxcr; /* XScale auxiliary control register.  */
        uint32_t c1_scr; /* secure config register.  */
        uint32_t c2_base0; /* MMU translation table base 0.  */
        uint32_t c2_base0_hi; /* MMU translation table base 0, high 32 bits */
        uint32_t c2_base1; /* MMU translation table base 0.  */
        uint32_t c2_base1_hi; /* MMU translation table base 1, high 32 bits */
        uint32_t c2_control; /* MMU translation table base control.  */
        uint32_t c2_mask; /* MMU translation table base selection mask.  */
        uint32_t c2_base_mask; /* MMU translation table base 0 mask. */
        uint32_t c2_data; /* MPU data cachable bits.  */
        uint32_t c2_insn; /* MPU instruction cachable bits.  */
        uint32_t c3; /* MMU domain access control register
                        MPU write buffer control.  */
        uint32_t c5_insn; /* Fault status registers.  */
        uint32_t c5_data;
        uint32_t c6_region[8]; /* MPU base/size registers.  */
        uint32_t c6_insn; /* Fault address registers.  */
        uint32_t c6_data;
        uint32_t c7_par;  /* Translation result. */
        uint32_t c7_par_hi;  /* Translation result, high 32 bits */
        uint32_t c9_insn; /* Cache lockdown registers.  */
        uint32_t c9_data;
        uint32_t c9_pmcr; /* performance monitor control register */
        uint32_t c9_pmcnten; /* perf monitor counter enables */
        uint32_t c9_pmovsr; /* perf monitor overflow status */
        uint32_t c9_pmxevtyper; /* perf monitor event type */
        uint32_t c9_pmuserenr; /* perf monitor user enable */
        uint32_t c9_pminten; /* perf monitor interrupt enables */
        uint32_t c13_fcse; /* FCSE PID.  */
        uint32_t c13_context; /* Context ID.  */
        uint32_t c13_tls1; /* User RW Thread register.  */
        uint32_t c13_tls2; /* User RO Thread register.  */
        uint32_t c13_tls3; /* Privileged Thread register.  */
        uint32_t c15_cpar; /* XScale Coprocessor Access Register */
        uint32_t c15_ticonfig; /* TI925T configuration byte.  */
        uint32_t c15_i_max; /* Maximum D-cache dirty line index.  */
        uint32_t c15_i_min; /* Minimum D-cache dirty line index.  */
        uint32_t c15_threadid; /* TI debugger thread-ID.  */
        uint32_t c15_config_base_address; /* SCU base address.  */
        uint32_t c15_diagnostic; /* diagnostic register */
        uint32_t c15_power_diagnostic;
        uint32_t c15_power_control; /* power control */
    } cp15;

    struct {
        uint32_t other_sp;
        uint32_t vecbase;
        uint32_t basepri;
        uint32_t control;
        int current_sp;
        int exception;
        int pending_exception;
    } v7m;

    /* Thumb-2 EE state.  */
    uint32_t teecr;
    uint32_t teehbr;

    /* VFP coprocessor state.  */
    struct {
        float64 regs[32];

        uint32_t xregs[16];
        /* We store these fpcsr fields separately for convenience.  */
        int vec_len;
        int vec_stride;

        /* scratch space when Tn are not sufficient.  */
        uint32_t scratch[8];

        /* fp_status is the "normal" fp status. standard_fp_status retains
         * values corresponding to the ARM "Standard FPSCR Value", ie
         * default-NaN, flush-to-zero, round-to-nearest and is used by
         * any operations (generally Neon) which the architecture defines
         * as controlled by the standard FPSCR value rather than the FPSCR.
         *
         * To avoid having to transfer exception bits around, we simply
         * say that the FPSCR cumulative exception flags are the logical
         * OR of the flags in the two fp statuses. This relies on the
         * only thing which needs to read the exception flags being
         * an explicit FPSCR read.
         */
        float_status fp_status;
        float_status standard_fp_status;
    } vfp;
    uint32_t exclusive_addr;
    uint32_t exclusive_val;
    uint32_t exclusive_high;
#if defined(CONFIG_USER_ONLY)
    uint32_t exclusive_test;
    uint32_t exclusive_info;
#endif

    /* iwMMXt coprocessor state.  */
    struct {
        uint64_t regs[16];
        uint64_t val;

        uint32_t cregs[16];
    } iwmmxt;

    /* For mixed endian mode.  */
    bool bswap_code;

#if defined(CONFIG_USER_ONLY)
    /* For usermode syscall translation.  */
    int eabi;
#endif

    CPU_COMMON

    /* These fields after the common ones so they are preserved on reset.  */

    /* Internal CPU feature flags.  */
    uint64_t features;

    void *nvic;
    const struct arm_boot_info *boot_info;
} CPUARMState;

#include "cpu-qom.h"

ARMCPU *cpu_arm_init(const char *cpu_model);
void arm_translate_init(void);
void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu);
int cpu_arm_exec(CPUARMState *s);
int bank_number(int mode);
void switch_mode(CPUARMState *, int);
uint32_t do_arm_semihosting(CPUARMState *env);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_arm_signal_handler(int host_signum, void *pinfo,
                           void *puc);
int cpu_arm_handle_mmu_fault (CPUARMState *env, target_ulong address, int rw,
                              int mmu_idx);
#define cpu_handle_mmu_fault cpu_arm_handle_mmu_fault

static inline void cpu_set_tls(CPUARMState *env, target_ulong newtls)
{
  env->cp15.c13_tls2 = newtls;
}

#define CPSR_M (0x1f)
#define CPSR_T (1 << 5)
#define CPSR_F (1 << 6)
#define CPSR_I (1 << 7)
#define CPSR_A (1 << 8)
#define CPSR_E (1 << 9)
#define CPSR_IT_2_7 (0xfc00)
#define CPSR_GE (0xf << 16)
#define CPSR_RESERVED (0xf << 20)
#define CPSR_J (1 << 24)
#define CPSR_IT_0_1 (3 << 25)
#define CPSR_Q (1 << 27)
#define CPSR_V (1 << 28)
#define CPSR_C (1 << 29)
#define CPSR_Z (1 << 30)
#define CPSR_N (1 << 31)
#define CPSR_NZCV (CPSR_N | CPSR_Z | CPSR_C | CPSR_V)

#define CPSR_IT (CPSR_IT_0_1 | CPSR_IT_2_7)
#define CACHED_CPSR_BITS (CPSR_T | CPSR_GE | CPSR_IT | CPSR_Q | CPSR_NZCV)
/* Bits writable in user mode.  */
#define CPSR_USER (CPSR_NZCV | CPSR_Q | CPSR_GE)
/* Execution state bits.  MRS read as zero, MSR writes ignored.  */
#define CPSR_EXEC (CPSR_T | CPSR_IT | CPSR_J)

/* Return the current CPSR value.  */
uint32_t cpsr_read(CPUARMState *env);
/* Set the CPSR.  Note that some bits of mask must be all-set or all-clear.  */
void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask);

/* Return the current xPSR value.  */
static inline uint32_t xpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return (env->NF & 0x80000000) | (ZF << 30)
        | (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 24) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | env->v7m.exception;
}

/* Set the xPSR.  Note that some bits of mask must be all-set or all-clear.  */
static inline void xpsr_write(CPUARMState *env, uint32_t val, uint32_t mask)
{
    if (mask & CPSR_NZCV) {
        env->ZF = (~val) & CPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & CPSR_Q)
        env->QF = ((val & CPSR_Q) != 0);
    if (mask & (1 << 24))
        env->thumb = ((val & (1 << 24)) != 0);
    if (mask & CPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & CPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & 0x1ff) {
        env->v7m.exception = val & 0x1ff;
    }
}

/* Return the current FPSCR value.  */
uint32_t vfp_get_fpscr(CPUARMState *env);
void vfp_set_fpscr(CPUARMState *env, uint32_t val);

enum arm_cpu_mode {
  ARM_CPU_MODE_USR = 0x10,
  ARM_CPU_MODE_FIQ = 0x11,
  ARM_CPU_MODE_IRQ = 0x12,
  ARM_CPU_MODE_SVC = 0x13,
  ARM_CPU_MODE_ABT = 0x17,
  ARM_CPU_MODE_UND = 0x1b,
  ARM_CPU_MODE_SYS = 0x1f
};

/* VFP system registers.  */
#define ARM_VFP_FPSID   0
#define ARM_VFP_FPSCR   1
#define ARM_VFP_MVFR1   6
#define ARM_VFP_MVFR0   7
#define ARM_VFP_FPEXC   8
#define ARM_VFP_FPINST  9
#define ARM_VFP_FPINST2 10

/* iwMMXt coprocessor control registers.  */
#define ARM_IWMMXT_wCID		0
#define ARM_IWMMXT_wCon		1
#define ARM_IWMMXT_wCSSF	2
#define ARM_IWMMXT_wCASF	3
#define ARM_IWMMXT_wCGR0	8
#define ARM_IWMMXT_wCGR1	9
#define ARM_IWMMXT_wCGR2	10
#define ARM_IWMMXT_wCGR3	11

/* If adding a feature bit which corresponds to a Linux ELF
 * HWCAP bit, remember to update the feature-bit-to-hwcap
 * mapping in linux-user/elfload.c:get_elf_hwcap().
 */
enum arm_features {
    ARM_FEATURE_VFP,
    ARM_FEATURE_AUXCR,  /* ARM1026 Auxiliary control register.  */
    ARM_FEATURE_XSCALE, /* Intel XScale extensions.  */
    ARM_FEATURE_IWMMXT, /* Intel iwMMXt extension.  */
    ARM_FEATURE_V6,
    ARM_FEATURE_V6K,
    ARM_FEATURE_V7,
    ARM_FEATURE_THUMB2,
    ARM_FEATURE_MPU,    /* Only has Memory Protection Unit, not full MMU.  */
    ARM_FEATURE_VFP3,
    ARM_FEATURE_VFP_FP16,
    ARM_FEATURE_NEON,
    ARM_FEATURE_THUMB_DIV, /* divide supported in Thumb encoding */
    ARM_FEATURE_M, /* Microcontroller profile.  */
    ARM_FEATURE_OMAPCP, /* OMAP specific CP15 ops handling.  */
    ARM_FEATURE_THUMB2EE,
    ARM_FEATURE_V7MP,    /* v7 Multiprocessing Extensions */
    ARM_FEATURE_V4T,
    ARM_FEATURE_V5,
    ARM_FEATURE_STRONGARM,
    ARM_FEATURE_VAPA, /* cp15 VA to PA lookups */
    ARM_FEATURE_ARM_DIV, /* divide supported in ARM encoding */
    ARM_FEATURE_VFP4, /* VFPv4 (implies that NEON is v2) */
    ARM_FEATURE_GENERIC_TIMER,
    ARM_FEATURE_MVFR, /* Media and VFP Feature Registers 0 and 1 */
    ARM_FEATURE_DUMMY_C15_REGS, /* RAZ/WI all of cp15 crn=15 */
    ARM_FEATURE_CACHE_TEST_CLEAN, /* 926/1026 style test-and-clean ops */
    ARM_FEATURE_CACHE_DIRTY_REG, /* 1136/1176 cache dirty status register */
    ARM_FEATURE_CACHE_BLOCK_OPS, /* v6 optional cache block operations */
    ARM_FEATURE_MPIDR, /* has cp15 MPIDR */
    ARM_FEATURE_PXN, /* has Privileged Execute Never bit */
    ARM_FEATURE_LPAE, /* has Large Physical Address Extension */
};

static inline int arm_feature(CPUARMState *env, int feature)
{
    return (env->features & (1ULL << feature)) != 0;
}

void arm_cpu_list(FILE *f, fprintf_function cpu_fprintf);

/* Interface between CPU and Interrupt controller.  */
void armv7m_nvic_set_pending(void *opaque, int irq);
int armv7m_nvic_acknowledge_irq(void *opaque);
void armv7m_nvic_complete_irq(void *opaque, int irq);

/* Interface for defining coprocessor registers.
 * Registers are defined in tables of arm_cp_reginfo structs
 * which are passed to define_arm_cp_regs().
 */

/* When looking up a coprocessor register we look for it
 * via an integer which encodes all of:
 *  coprocessor number
 *  Crn, Crm, opc1, opc2 fields
 *  32 or 64 bit register (ie is it accessed via MRC/MCR
 *    or via MRRC/MCRR?)
 * We allow 4 bits for opc1 because MRRC/MCRR have a 4 bit field.
 * (In this case crn and opc2 should be zero.)
 */
#define ENCODE_CP_REG(cp, is64, crn, crm, opc1, opc2)   \
    (((cp) << 16) | ((is64) << 15) | ((crn) << 11) |    \
     ((crm) << 7) | ((opc1) << 3) | (opc2))

/* ARMCPRegInfo type field bits. If the SPECIAL bit is set this is a
 * special-behaviour cp reg and bits [15..8] indicate what behaviour
 * it has. Otherwise it is a simple cp reg, where CONST indicates that
 * TCG can assume the value to be constant (ie load at translate time)
 * and 64BIT indicates a 64 bit wide coprocessor register. SUPPRESS_TB_END
 * indicates that the TB should not be ended after a write to this register
 * (the default is that the TB ends after cp writes). OVERRIDE permits
 * a register definition to override a previous definition for the
 * same (cp, is64, crn, crm, opc1, opc2) tuple: either the new or the
 * old must have the OVERRIDE bit set.
 */
#define ARM_CP_SPECIAL 1
#define ARM_CP_CONST 2
#define ARM_CP_64BIT 4
#define ARM_CP_SUPPRESS_TB_END 8
#define ARM_CP_OVERRIDE 16
#define ARM_CP_NOP (ARM_CP_SPECIAL | (1 << 8))
#define ARM_CP_WFI (ARM_CP_SPECIAL | (2 << 8))
#define ARM_LAST_SPECIAL ARM_CP_WFI
/* Used only as a terminator for ARMCPRegInfo lists */
#define ARM_CP_SENTINEL 0xffff
/* Mask of only the flag bits in a type field */
#define ARM_CP_FLAG_MASK 0x1f

/* Return true if cptype is a valid type field. This is used to try to
 * catch errors where the sentinel has been accidentally left off the end
 * of a list of registers.
 */
static inline bool cptype_valid(int cptype)
{
    return ((cptype & ~ARM_CP_FLAG_MASK) == 0)
        || ((cptype & ARM_CP_SPECIAL) &&
            (cptype <= ARM_LAST_SPECIAL));
}

/* Access rights:
 * We define bits for Read and Write access for what rev C of the v7-AR ARM ARM
 * defines as PL0 (user), PL1 (fiq/irq/svc/abt/und/sys, ie privileged), and
 * PL2 (hyp). The other level which has Read and Write bits is Secure PL1
 * (ie any of the privileged modes in Secure state, or Monitor mode).
 * If a register is accessible in one privilege level it's always accessible
 * in higher privilege levels too. Since "Secure PL1" also follows this rule
 * (ie anything visible in PL2 is visible in S-PL1, some things are only
 * visible in S-PL1) but "Secure PL1" is a bit of a mouthful, we bend the
 * terminology a little and call this PL3.
 *
 * If access permissions for a register are more complex than can be
 * described with these bits, then use a laxer set of restrictions, and
 * do the more restrictive/complex check inside a helper function.
 */
#define PL3_R 0x80
#define PL3_W 0x40
#define PL2_R (0x20 | PL3_R)
#define PL2_W (0x10 | PL3_W)
#define PL1_R (0x08 | PL2_R)
#define PL1_W (0x04 | PL2_W)
#define PL0_R (0x02 | PL1_R)
#define PL0_W (0x01 | PL1_W)

#define PL3_RW (PL3_R | PL3_W)
#define PL2_RW (PL2_R | PL2_W)
#define PL1_RW (PL1_R | PL1_W)
#define PL0_RW (PL0_R | PL0_W)

static inline int arm_current_pl(CPUARMState *env)
{
    if ((env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_USR) {
        return 0;
    }
    /* We don't currently implement the Virtualization or TrustZone
     * extensions, so PL2 and PL3 don't exist for us.
     */
    return 1;
}

typedef struct ARMCPRegInfo ARMCPRegInfo;

/* Access functions for coprocessor registers. These should return
 * 0 on success, or one of the EXCP_* constants if access should cause
 * an exception (in which case *value is not written).
 */
typedef int CPReadFn(CPUARMState *env, const ARMCPRegInfo *opaque,
                     uint64_t *value);
typedef int CPWriteFn(CPUARMState *env, const ARMCPRegInfo *opaque,
                      uint64_t value);
/* Hook function for register reset */
typedef void CPResetFn(CPUARMState *env, const ARMCPRegInfo *opaque);

#define CP_ANY 0xff

/* Definition of an ARM coprocessor register */
struct ARMCPRegInfo {
    /* Name of register (useful mainly for debugging, need not be unique) */
    const char *name;
    /* Location of register: coprocessor number and (crn,crm,opc1,opc2)
     * tuple. Any of crm, opc1 and opc2 may be CP_ANY to indicate a
     * 'wildcard' field -- any value of that field in the MRC/MCR insn
     * will be decoded to this register. The register read and write
     * callbacks will be passed an ARMCPRegInfo with the crn/crm/opc1/opc2
     * used by the program, so it is possible to register a wildcard and
     * then behave differently on read/write if necessary.
     * For 64 bit registers, only crm and opc1 are relevant; crn and opc2
     * must both be zero.
     */
    uint8_t cp;
    uint8_t crn;
    uint8_t crm;
    uint8_t opc1;
    uint8_t opc2;
    /* Register type: ARM_CP_* bits/values */
    int type;
    /* Access rights: PL*_[RW] */
    int access;
    /* The opaque pointer passed to define_arm_cp_regs_with_opaque() when
     * this register was defined: can be used to hand data through to the
     * register read/write functions, since they are passed the ARMCPRegInfo*.
     */
    void *opaque;
    /* Value of this register, if it is ARM_CP_CONST. Otherwise, if
     * fieldoffset is non-zero, the reset value of the register.
     */
    uint64_t resetvalue;
    /* Offset of the field in CPUARMState for this register. This is not
     * needed if either:
     *  1. type is ARM_CP_CONST or one of the ARM_CP_SPECIALs
     *  2. both readfn and writefn are specified
     */
    ptrdiff_t fieldoffset; /* offsetof(CPUARMState, field) */
    /* Function for handling reads of this register. If NULL, then reads
     * will be done by loading from the offset into CPUARMState specified
     * by fieldoffset.
     */
    CPReadFn *readfn;
    /* Function for handling writes of this register. If NULL, then writes
     * will be done by writing to the offset into CPUARMState specified
     * by fieldoffset.
     */
    CPWriteFn *writefn;
    /* Function for resetting the register. If NULL, then reset will be done
     * by writing resetvalue to the field specified in fieldoffset. If
     * fieldoffset is 0 then no reset will be done.
     */
    CPResetFn *resetfn;
};

/* Macros which are lvalues for the field in CPUARMState for the
 * ARMCPRegInfo *ri.
 */
#define CPREG_FIELD32(env, ri) \
    (*(uint32_t *)((char *)(env) + (ri)->fieldoffset))
#define CPREG_FIELD64(env, ri) \
    (*(uint64_t *)((char *)(env) + (ri)->fieldoffset))

#define REGINFO_SENTINEL { .type = ARM_CP_SENTINEL }

void define_arm_cp_regs_with_opaque(ARMCPU *cpu,
                                    const ARMCPRegInfo *regs, void *opaque);
void define_one_arm_cp_reg_with_opaque(ARMCPU *cpu,
                                       const ARMCPRegInfo *regs, void *opaque);
static inline void define_arm_cp_regs(ARMCPU *cpu, const ARMCPRegInfo *regs)
{
    define_arm_cp_regs_with_opaque(cpu, regs, 0);
}
static inline void define_one_arm_cp_reg(ARMCPU *cpu, const ARMCPRegInfo *regs)
{
    define_one_arm_cp_reg_with_opaque(cpu, regs, 0);
}
const ARMCPRegInfo *get_arm_cp_reginfo(ARMCPU *cpu, uint32_t encoded_cp);

/* CPWriteFn that can be used to implement writes-ignored behaviour */
int arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value);
/* CPReadFn that can be used for read-as-zero behaviour */
int arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t *value);

static inline bool cp_access_ok(CPUARMState *env,
                                const ARMCPRegInfo *ri, int isread)
{
    return (ri->access >> ((arm_current_pl(env) * 2) + isread)) & 1;
}

/* Does the core conform to the the "MicroController" profile. e.g. Cortex-M3.
   Note the M in older cores (eg. ARM7TDMI) stands for Multiply. These are
   conventional cores (ie. Application or Realtime profile).  */

#define IS_M(env) arm_feature(env, ARM_FEATURE_M)

#define ARM_CPUID_TI915T      0x54029152
#define ARM_CPUID_TI925T      0x54029252

#if defined(CONFIG_USER_ONLY)
#define TARGET_PAGE_BITS 12
#else
/* The ARM MMU allows 1k pages.  */
/* ??? Linux doesn't actually use these, and they're deprecated in recent
   architecture revisions.  Maybe a configure option to disable them.  */
#define TARGET_PAGE_BITS 10
#endif

#define TARGET_PHYS_ADDR_SPACE_BITS 40
#define TARGET_VIRT_ADDR_SPACE_BITS 32

static inline CPUARMState *cpu_init(const char *cpu_model)
{
    ARMCPU *cpu = cpu_arm_init(cpu_model);
    if (cpu) {
        return &cpu->env;
    }
    return NULL;
}

#define cpu_exec cpu_arm_exec
#define cpu_gen_code cpu_arm_gen_code
#define cpu_signal_handler cpu_arm_signal_handler
#define cpu_list arm_cpu_list

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUARMState *env)
{
    return (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR ? 1 : 0;
}

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp)
        env->regs[13] = newsp;
    env->regs[0] = 0;
}
#endif

#include "exec/cpu-all.h"

/* Bit usage in the TB flags field: */
#define ARM_TBFLAG_THUMB_SHIFT      0
#define ARM_TBFLAG_THUMB_MASK       (1 << ARM_TBFLAG_THUMB_SHIFT)
#define ARM_TBFLAG_VECLEN_SHIFT     1
#define ARM_TBFLAG_VECLEN_MASK      (0x7 << ARM_TBFLAG_VECLEN_SHIFT)
#define ARM_TBFLAG_VECSTRIDE_SHIFT  4
#define ARM_TBFLAG_VECSTRIDE_MASK   (0x3 << ARM_TBFLAG_VECSTRIDE_SHIFT)
#define ARM_TBFLAG_PRIV_SHIFT       6
#define ARM_TBFLAG_PRIV_MASK        (1 << ARM_TBFLAG_PRIV_SHIFT)
#define ARM_TBFLAG_VFPEN_SHIFT      7
#define ARM_TBFLAG_VFPEN_MASK       (1 << ARM_TBFLAG_VFPEN_SHIFT)
#define ARM_TBFLAG_CONDEXEC_SHIFT   8
#define ARM_TBFLAG_CONDEXEC_MASK    (0xff << ARM_TBFLAG_CONDEXEC_SHIFT)
#define ARM_TBFLAG_BSWAP_CODE_SHIFT 16
#define ARM_TBFLAG_BSWAP_CODE_MASK  (1 << ARM_TBFLAG_BSWAP_CODE_SHIFT)
/* Bits 31..17 are currently unused. */

/* some convenience accessor macros */
#define ARM_TBFLAG_THUMB(F) \
    (((F) & ARM_TBFLAG_THUMB_MASK) >> ARM_TBFLAG_THUMB_SHIFT)
#define ARM_TBFLAG_VECLEN(F) \
    (((F) & ARM_TBFLAG_VECLEN_MASK) >> ARM_TBFLAG_VECLEN_SHIFT)
#define ARM_TBFLAG_VECSTRIDE(F) \
    (((F) & ARM_TBFLAG_VECSTRIDE_MASK) >> ARM_TBFLAG_VECSTRIDE_SHIFT)
#define ARM_TBFLAG_PRIV(F) \
    (((F) & ARM_TBFLAG_PRIV_MASK) >> ARM_TBFLAG_PRIV_SHIFT)
#define ARM_TBFLAG_VFPEN(F) \
    (((F) & ARM_TBFLAG_VFPEN_MASK) >> ARM_TBFLAG_VFPEN_SHIFT)
#define ARM_TBFLAG_CONDEXEC(F) \
    (((F) & ARM_TBFLAG_CONDEXEC_MASK) >> ARM_TBFLAG_CONDEXEC_SHIFT)
#define ARM_TBFLAG_BSWAP_CODE(F) \
    (((F) & ARM_TBFLAG_BSWAP_CODE_MASK) >> ARM_TBFLAG_BSWAP_CODE_SHIFT)

static inline void cpu_get_tb_cpu_state(CPUARMState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    int privmode;
    *pc = env->regs[15];
    *cs_base = 0;
    *flags = (env->thumb << ARM_TBFLAG_THUMB_SHIFT)
        | (env->vfp.vec_len << ARM_TBFLAG_VECLEN_SHIFT)
        | (env->vfp.vec_stride << ARM_TBFLAG_VECSTRIDE_SHIFT)
        | (env->condexec_bits << ARM_TBFLAG_CONDEXEC_SHIFT)
        | (env->bswap_code << ARM_TBFLAG_BSWAP_CODE_SHIFT);
    if (arm_feature(env, ARM_FEATURE_M)) {
        privmode = !((env->v7m.exception == 0) && (env->v7m.control & 1));
    } else {
        privmode = (env->uncached_cpsr & CPSR_M) != ARM_CPU_MODE_USR;
    }
    if (privmode) {
        *flags |= ARM_TBFLAG_PRIV_MASK;
    }
    if (env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) {
        *flags |= ARM_TBFLAG_VFPEN_MASK;
    }
}

static inline bool cpu_has_work(CPUState *cpu)
{
    return cpu->interrupt_request &
        (CPU_INTERRUPT_FIQ | CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB);
}

#include "exec/exec-all.h"

static inline void cpu_pc_from_tb(CPUARMState *env, TranslationBlock *tb)
{
    env->regs[15] = tb->pc;
}

/* Load an instruction and return it in the standard little-endian order */
static inline uint32_t arm_ldl_code(CPUARMState *env, uint32_t addr,
                                    bool do_swap)
{
    uint32_t insn = cpu_ldl_code(env, addr);
    if (do_swap) {
        return bswap32(insn);
    }
    return insn;
}

/* Ditto, for a halfword (Thumb) instruction */
static inline uint16_t arm_lduw_code(CPUARMState *env, uint32_t addr,
                                     bool do_swap)
{
    uint16_t insn = cpu_lduw_code(env, addr);
    if (do_swap) {
        return bswap16(insn);
    }
    return insn;
}

#endif
