/*
 * ARM virtual CPU header
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARM_CPU_H
#define ARM_CPU_H

#include "kvm-consts.h"
#include "hw/registerfields.h"
#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "qapi/qapi-types-common.h"

/* ARM processors have a weak memory model */
#define TCG_GUEST_DEFAULT_MO      (0)

#ifdef TARGET_AARCH64
#define KVM_HAVE_MCE_INJECTION 1
#endif

#define EXCP_UDEF            1   /* undefined instruction */
#define EXCP_SWI             2   /* software interrupt */
#define EXCP_PREFETCH_ABORT  3
#define EXCP_DATA_ABORT      4
#define EXCP_IRQ             5
#define EXCP_FIQ             6
#define EXCP_BKPT            7
#define EXCP_EXCEPTION_EXIT  8   /* Return from v7M exception.  */
#define EXCP_KERNEL_TRAP     9   /* Jumped to kernel code page.  */
#define EXCP_HVC            11   /* HyperVisor Call */
#define EXCP_HYP_TRAP       12
#define EXCP_SMC            13   /* Secure Monitor Call */
#define EXCP_VIRQ           14
#define EXCP_VFIQ           15
#define EXCP_SEMIHOST       16   /* semihosting call */
#define EXCP_NOCP           17   /* v7M NOCP UsageFault */
#define EXCP_INVSTATE       18   /* v7M INVSTATE UsageFault */
#define EXCP_STKOF          19   /* v8M STKOF UsageFault */
#define EXCP_LAZYFP         20   /* v7M fault during lazy FP stacking */
#define EXCP_LSERR          21   /* v8M LSERR SecureFault */
#define EXCP_UNALIGNED      22   /* v7M UNALIGNED UsageFault */
#define EXCP_DIVBYZERO      23   /* v7M DIVBYZERO UsageFault */
/* NB: add new EXCP_ defines to the array in arm_log_exception() too */

#define ARMV7M_EXCP_RESET   1
#define ARMV7M_EXCP_NMI     2
#define ARMV7M_EXCP_HARD    3
#define ARMV7M_EXCP_MEM     4
#define ARMV7M_EXCP_BUS     5
#define ARMV7M_EXCP_USAGE   6
#define ARMV7M_EXCP_SECURE  7
#define ARMV7M_EXCP_SVC     11
#define ARMV7M_EXCP_DEBUG   12
#define ARMV7M_EXCP_PENDSV  14
#define ARMV7M_EXCP_SYSTICK 15

/* For M profile, some registers are banked secure vs non-secure;
 * these are represented as a 2-element array where the first element
 * is the non-secure copy and the second is the secure copy.
 * When the CPU does not have implement the security extension then
 * only the first element is used.
 * This means that the copy for the current security state can be
 * accessed via env->registerfield[env->v7m.secure] (whether the security
 * extension is implemented or not).
 */
enum {
    M_REG_NS = 0,
    M_REG_S = 1,
    M_REG_NUM_BANKS = 2,
};

/* ARM-specific interrupt pending bits.  */
#define CPU_INTERRUPT_FIQ   CPU_INTERRUPT_TGT_EXT_1
#define CPU_INTERRUPT_VIRQ  CPU_INTERRUPT_TGT_EXT_2
#define CPU_INTERRUPT_VFIQ  CPU_INTERRUPT_TGT_EXT_3

/* The usual mapping for an AArch64 system register to its AArch32
 * counterpart is for the 32 bit world to have access to the lower
 * half only (with writes leaving the upper half untouched). It's
 * therefore useful to be able to pass TCG the offset of the least
 * significant half of a uint64_t struct member.
 */
#ifdef HOST_WORDS_BIGENDIAN
#define offsetoflow32(S, M) (offsetof(S, M) + sizeof(uint32_t))
#define offsetofhigh32(S, M) offsetof(S, M)
#else
#define offsetoflow32(S, M) offsetof(S, M)
#define offsetofhigh32(S, M) (offsetof(S, M) + sizeof(uint32_t))
#endif

/* Meanings of the ARMCPU object's four inbound GPIO lines */
#define ARM_CPU_IRQ 0
#define ARM_CPU_FIQ 1
#define ARM_CPU_VIRQ 2
#define ARM_CPU_VFIQ 3

/* ARM-specific extra insn start words:
 * 1: Conditional execution bits
 * 2: Partial exception syndrome for data aborts
 */
#define TARGET_INSN_START_EXTRA_WORDS 2

/* The 2nd extra word holding syndrome info for data aborts does not use
 * the upper 6 bits nor the lower 14 bits. We mask and shift it down to
 * help the sleb128 encoder do a better job.
 * When restoring the CPU state, we shift it back up.
 */
#define ARM_INSN_START_WORD2_MASK ((1 << 26) - 1)
#define ARM_INSN_START_WORD2_SHIFT 14

/* We currently assume float and double are IEEE single and double
   precision respectively.
   Doing runtime conversions is tricky because VFP registers may contain
   integer values (eg. as the result of a FTOSI instruction).
   s<2n> maps to the least significant half of d<n>
   s<2n+1> maps to the most significant half of d<n>
 */

/**
 * DynamicGDBXMLInfo:
 * @desc: Contains the XML descriptions.
 * @num: Number of the registers in this XML seen by GDB.
 * @data: A union with data specific to the set of registers
 *    @cpregs_keys: Array that contains the corresponding Key of
 *                  a given cpreg with the same order of the cpreg
 *                  in the XML description.
 */
typedef struct DynamicGDBXMLInfo {
    char *desc;
    int num;
    union {
        struct {
            uint32_t *keys;
        } cpregs;
    } data;
} DynamicGDBXMLInfo;

/* CPU state for each instance of a generic timer (in cp15 c14) */
typedef struct ARMGenericTimer {
    uint64_t cval; /* Timer CompareValue register */
    uint64_t ctl; /* Timer Control register */
} ARMGenericTimer;

#define GTIMER_PHYS     0
#define GTIMER_VIRT     1
#define GTIMER_HYP      2
#define GTIMER_SEC      3
#define GTIMER_HYPVIRT  4
#define NUM_GTIMERS     5

typedef struct {
    uint64_t raw_tcr;
    uint32_t mask;
    uint32_t base_mask;
} TCR;

#define VTCR_NSW (1u << 29)
#define VTCR_NSA (1u << 30)
#define VSTCR_SW VTCR_NSW
#define VSTCR_SA VTCR_NSA

/* Define a maximum sized vector register.
 * For 32-bit, this is a 128-bit NEON/AdvSIMD register.
 * For 64-bit, this is a 2048-bit SVE register.
 *
 * Note that the mapping between S, D, and Q views of the register bank
 * differs between AArch64 and AArch32.
 * In AArch32:
 *  Qn = regs[n].d[1]:regs[n].d[0]
 *  Dn = regs[n / 2].d[n & 1]
 *  Sn = regs[n / 4].d[n % 4 / 2],
 *       bits 31..0 for even n, and bits 63..32 for odd n
 *       (and regs[16] to regs[31] are inaccessible)
 * In AArch64:
 *  Zn = regs[n].d[*]
 *  Qn = regs[n].d[1]:regs[n].d[0]
 *  Dn = regs[n].d[0]
 *  Sn = regs[n].d[0] bits 31..0
 *  Hn = regs[n].d[0] bits 15..0
 *
 * This corresponds to the architecturally defined mapping between
 * the two execution states, and means we do not need to explicitly
 * map these registers when changing states.
 *
 * Align the data for use with TCG host vector operations.
 */

#ifdef TARGET_AARCH64
# define ARM_MAX_VQ    16
void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp);
void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp);
void arm_cpu_lpa2_finalize(ARMCPU *cpu, Error **errp);
#else
# define ARM_MAX_VQ    1
static inline void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp) { }
static inline void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp) { }
static inline void arm_cpu_lpa2_finalize(ARMCPU *cpu, Error **errp) { }
#endif

typedef struct ARMVectorReg {
    uint64_t d[2 * ARM_MAX_VQ] QEMU_ALIGNED(16);
} ARMVectorReg;

#ifdef TARGET_AARCH64
/* In AArch32 mode, predicate registers do not exist at all.  */
typedef struct ARMPredicateReg {
    uint64_t p[DIV_ROUND_UP(2 * ARM_MAX_VQ, 8)] QEMU_ALIGNED(16);
} ARMPredicateReg;

/* In AArch32 mode, PAC keys do not exist at all.  */
typedef struct ARMPACKey {
    uint64_t lo, hi;
} ARMPACKey;
#endif

/* See the commentary above the TBFLAG field definitions.  */
typedef struct CPUARMTBFlags {
    uint32_t flags;
    target_ulong flags2;
} CPUARMTBFlags;

typedef struct CPUArchState {
    /* Regs for current mode.  */
    uint32_t regs[16];

    /* 32/64 switch only happens when taking and returning from
     * exceptions so the overlap semantics are taken care of then
     * instead of having a complicated union.
     */
    /* Regs for A64 mode.  */
    uint64_t xregs[32];
    uint64_t pc;
    /* PSTATE isn't an architectural register for ARMv8. However, it is
     * convenient for us to assemble the underlying state into a 32 bit format
     * identical to the architectural format used for the SPSR. (This is also
     * what the Linux kernel's 'pstate' field in signal handlers and KVM's
     * 'pstate' register are.) Of the PSTATE bits:
     *  NZCV are kept in the split out env->CF/VF/NF/ZF, (which have the same
     *    semantics as for AArch32, as described in the comments on each field)
     *  nRW (also known as M[4]) is kept, inverted, in env->aarch64
     *  DAIF (exception masks) are kept in env->daif
     *  BTYPE is kept in env->btype
     *  all other bits are stored in their correct places in env->pstate
     */
    uint32_t pstate;
    uint32_t aarch64; /* 1 if CPU is in aarch64 state; inverse of PSTATE.nRW */

    /* Cached TBFLAGS state.  See below for which bits are included.  */
    CPUARMTBFlags hflags;

    /* Frequently accessed CPSR bits are stored separately for efficiency.
       This contains all the other bits.  Use cpsr_{read,write} to access
       the whole CPSR.  */
    uint32_t uncached_cpsr;
    uint32_t spsr;

    /* Banked registers.  */
    uint64_t banked_spsr[8];
    uint32_t banked_r13[8];
    uint32_t banked_r14[8];

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
    uint32_t btype;  /* BTI branch type.  spsr[11:10].  */
    uint64_t daif; /* exception masks, in the bits they are in PSTATE */

    uint64_t elr_el[4]; /* AArch64 exception link regs  */
    uint64_t sp_el[4]; /* AArch64 banked stack pointers */

    /* System control coprocessor (cp15) */
    struct {
        uint32_t c0_cpuid;
        union { /* Cache size selection */
            struct {
                uint64_t _unused_csselr0;
                uint64_t csselr_ns;
                uint64_t _unused_csselr1;
                uint64_t csselr_s;
            };
            uint64_t csselr_el[4];
        };
        union { /* System control register. */
            struct {
                uint64_t _unused_sctlr;
                uint64_t sctlr_ns;
                uint64_t hsctlr;
                uint64_t sctlr_s;
            };
            uint64_t sctlr_el[4];
        };
        uint64_t cpacr_el1; /* Architectural feature access control register */
        uint64_t cptr_el[4];  /* ARMv8 feature trap registers */
        uint32_t c1_xscaleauxcr; /* XScale auxiliary control register.  */
        uint64_t sder; /* Secure debug enable register. */
        uint32_t nsacr; /* Non-secure access control register. */
        union { /* MMU translation table base 0. */
            struct {
                uint64_t _unused_ttbr0_0;
                uint64_t ttbr0_ns;
                uint64_t _unused_ttbr0_1;
                uint64_t ttbr0_s;
            };
            uint64_t ttbr0_el[4];
        };
        union { /* MMU translation table base 1. */
            struct {
                uint64_t _unused_ttbr1_0;
                uint64_t ttbr1_ns;
                uint64_t _unused_ttbr1_1;
                uint64_t ttbr1_s;
            };
            uint64_t ttbr1_el[4];
        };
        uint64_t vttbr_el2; /* Virtualization Translation Table Base.  */
        uint64_t vsttbr_el2; /* Secure Virtualization Translation Table. */
        /* MMU translation table base control. */
        TCR tcr_el[4];
        TCR vtcr_el2; /* Virtualization Translation Control.  */
        TCR vstcr_el2; /* Secure Virtualization Translation Control. */
        uint32_t c2_data; /* MPU data cacheable bits.  */
        uint32_t c2_insn; /* MPU instruction cacheable bits.  */
        union { /* MMU domain access control register
                 * MPU write buffer control.
                 */
            struct {
                uint64_t dacr_ns;
                uint64_t dacr_s;
            };
            struct {
                uint64_t dacr32_el2;
            };
        };
        uint32_t pmsav5_data_ap; /* PMSAv5 MPU data access permissions */
        uint32_t pmsav5_insn_ap; /* PMSAv5 MPU insn access permissions */
        uint64_t hcr_el2; /* Hypervisor configuration register */
        uint64_t scr_el3; /* Secure configuration register.  */
        union { /* Fault status registers.  */
            struct {
                uint64_t ifsr_ns;
                uint64_t ifsr_s;
            };
            struct {
                uint64_t ifsr32_el2;
            };
        };
        union {
            struct {
                uint64_t _unused_dfsr;
                uint64_t dfsr_ns;
                uint64_t hsr;
                uint64_t dfsr_s;
            };
            uint64_t esr_el[4];
        };
        uint32_t c6_region[8]; /* MPU base/size registers.  */
        union { /* Fault address registers. */
            struct {
                uint64_t _unused_far0;
#ifdef HOST_WORDS_BIGENDIAN
                uint32_t ifar_ns;
                uint32_t dfar_ns;
                uint32_t ifar_s;
                uint32_t dfar_s;
#else
                uint32_t dfar_ns;
                uint32_t ifar_ns;
                uint32_t dfar_s;
                uint32_t ifar_s;
#endif
                uint64_t _unused_far3;
            };
            uint64_t far_el[4];
        };
        uint64_t hpfar_el2;
        uint64_t hstr_el2;
        union { /* Translation result. */
            struct {
                uint64_t _unused_par_0;
                uint64_t par_ns;
                uint64_t _unused_par_1;
                uint64_t par_s;
            };
            uint64_t par_el[4];
        };

        uint32_t c9_insn; /* Cache lockdown registers.  */
        uint32_t c9_data;
        uint64_t c9_pmcr; /* performance monitor control register */
        uint64_t c9_pmcnten; /* perf monitor counter enables */
        uint64_t c9_pmovsr; /* perf monitor overflow status */
        uint64_t c9_pmuserenr; /* perf monitor user enable */
        uint64_t c9_pmselr; /* perf monitor counter selection register */
        uint64_t c9_pminten; /* perf monitor interrupt enables */
        union { /* Memory attribute redirection */
            struct {
#ifdef HOST_WORDS_BIGENDIAN
                uint64_t _unused_mair_0;
                uint32_t mair1_ns;
                uint32_t mair0_ns;
                uint64_t _unused_mair_1;
                uint32_t mair1_s;
                uint32_t mair0_s;
#else
                uint64_t _unused_mair_0;
                uint32_t mair0_ns;
                uint32_t mair1_ns;
                uint64_t _unused_mair_1;
                uint32_t mair0_s;
                uint32_t mair1_s;
#endif
            };
            uint64_t mair_el[4];
        };
        union { /* vector base address register */
            struct {
                uint64_t _unused_vbar;
                uint64_t vbar_ns;
                uint64_t hvbar;
                uint64_t vbar_s;
            };
            uint64_t vbar_el[4];
        };
        uint32_t mvbar; /* (monitor) vector base address register */
        uint64_t rvbar; /* rvbar sampled from rvbar property at reset */
        struct { /* FCSE PID. */
            uint32_t fcseidr_ns;
            uint32_t fcseidr_s;
        };
        union { /* Context ID. */
            struct {
                uint64_t _unused_contextidr_0;
                uint64_t contextidr_ns;
                uint64_t _unused_contextidr_1;
                uint64_t contextidr_s;
            };
            uint64_t contextidr_el[4];
        };
        union { /* User RW Thread register. */
            struct {
                uint64_t tpidrurw_ns;
                uint64_t tpidrprw_ns;
                uint64_t htpidr;
                uint64_t _tpidr_el3;
            };
            uint64_t tpidr_el[4];
        };
        /* The secure banks of these registers don't map anywhere */
        uint64_t tpidrurw_s;
        uint64_t tpidrprw_s;
        uint64_t tpidruro_s;

        union { /* User RO Thread register. */
            uint64_t tpidruro_ns;
            uint64_t tpidrro_el[1];
        };
        uint64_t c14_cntfrq; /* Counter Frequency register */
        uint64_t c14_cntkctl; /* Timer Control register */
        uint32_t cnthctl_el2; /* Counter/Timer Hyp Control register */
        uint64_t cntvoff_el2; /* Counter Virtual Offset register */
        ARMGenericTimer c14_timer[NUM_GTIMERS];
        uint32_t c15_cpar; /* XScale Coprocessor Access Register */
        uint32_t c15_ticonfig; /* TI925T configuration byte.  */
        uint32_t c15_i_max; /* Maximum D-cache dirty line index.  */
        uint32_t c15_i_min; /* Minimum D-cache dirty line index.  */
        uint32_t c15_threadid; /* TI debugger thread-ID.  */
        uint32_t c15_config_base_address; /* SCU base address.  */
        uint32_t c15_diagnostic; /* diagnostic register */
        uint32_t c15_power_diagnostic;
        uint32_t c15_power_control; /* power control */
        uint64_t dbgbvr[16]; /* breakpoint value registers */
        uint64_t dbgbcr[16]; /* breakpoint control registers */
        uint64_t dbgwvr[16]; /* watchpoint value registers */
        uint64_t dbgwcr[16]; /* watchpoint control registers */
        uint64_t mdscr_el1;
        uint64_t oslsr_el1; /* OS Lock Status */
        uint64_t mdcr_el2;
        uint64_t mdcr_el3;
        /* Stores the architectural value of the counter *the last time it was
         * updated* by pmccntr_op_start. Accesses should always be surrounded
         * by pmccntr_op_start/pmccntr_op_finish to guarantee the latest
         * architecturally-correct value is being read/set.
         */
        uint64_t c15_ccnt;
        /* Stores the delta between the architectural value and the underlying
         * cycle count during normal operation. It is used to update c15_ccnt
         * to be the correct architectural value before accesses. During
         * accesses, c15_ccnt_delta contains the underlying count being used
         * for the access, after which it reverts to the delta value in
         * pmccntr_op_finish.
         */
        uint64_t c15_ccnt_delta;
        uint64_t c14_pmevcntr[31];
        uint64_t c14_pmevcntr_delta[31];
        uint64_t c14_pmevtyper[31];
        uint64_t pmccfiltr_el0; /* Performance Monitor Filter Register */
        uint64_t vpidr_el2; /* Virtualization Processor ID Register */
        uint64_t vmpidr_el2; /* Virtualization Multiprocessor ID Register */
        uint64_t tfsr_el[4]; /* tfsre0_el1 is index 0.  */
        uint64_t gcr_el1;
        uint64_t rgsr_el1;
    } cp15;

    struct {
        /* M profile has up to 4 stack pointers:
         * a Main Stack Pointer and a Process Stack Pointer for each
         * of the Secure and Non-Secure states. (If the CPU doesn't support
         * the security extension then it has only two SPs.)
         * In QEMU we always store the currently active SP in regs[13],
         * and the non-active SP for the current security state in
         * v7m.other_sp. The stack pointers for the inactive security state
         * are stored in other_ss_msp and other_ss_psp.
         * switch_v7m_security_state() is responsible for rearranging them
         * when we change security state.
         */
        uint32_t other_sp;
        uint32_t other_ss_msp;
        uint32_t other_ss_psp;
        uint32_t vecbase[M_REG_NUM_BANKS];
        uint32_t basepri[M_REG_NUM_BANKS];
        uint32_t control[M_REG_NUM_BANKS];
        uint32_t ccr[M_REG_NUM_BANKS]; /* Configuration and Control */
        uint32_t cfsr[M_REG_NUM_BANKS]; /* Configurable Fault Status */
        uint32_t hfsr; /* HardFault Status */
        uint32_t dfsr; /* Debug Fault Status Register */
        uint32_t sfsr; /* Secure Fault Status Register */
        uint32_t mmfar[M_REG_NUM_BANKS]; /* MemManage Fault Address */
        uint32_t bfar; /* BusFault Address */
        uint32_t sfar; /* Secure Fault Address Register */
        unsigned mpu_ctrl[M_REG_NUM_BANKS]; /* MPU_CTRL */
        int exception;
        uint32_t primask[M_REG_NUM_BANKS];
        uint32_t faultmask[M_REG_NUM_BANKS];
        uint32_t aircr; /* only holds r/w state if security extn implemented */
        uint32_t secure; /* Is CPU in Secure state? (not guest visible) */
        uint32_t csselr[M_REG_NUM_BANKS];
        uint32_t scr[M_REG_NUM_BANKS];
        uint32_t msplim[M_REG_NUM_BANKS];
        uint32_t psplim[M_REG_NUM_BANKS];
        uint32_t fpcar[M_REG_NUM_BANKS];
        uint32_t fpccr[M_REG_NUM_BANKS];
        uint32_t fpdscr[M_REG_NUM_BANKS];
        uint32_t cpacr[M_REG_NUM_BANKS];
        uint32_t nsacr;
        uint32_t ltpsize;
        uint32_t vpr;
    } v7m;

    /* Information associated with an exception about to be taken:
     * code which raises an exception must set cs->exception_index and
     * the relevant parts of this structure; the cpu_do_interrupt function
     * will then set the guest-visible registers as part of the exception
     * entry process.
     */
    struct {
        uint32_t syndrome; /* AArch64 format syndrome register */
        uint32_t fsr; /* AArch32 format fault status register info */
        uint64_t vaddress; /* virtual addr associated with exception, if any */
        uint32_t target_el; /* EL the exception should be targeted for */
        /* If we implement EL2 we will also need to store information
         * about the intermediate physical address for stage 2 faults.
         */
    } exception;

    /* Information associated with an SError */
    struct {
        uint8_t pending;
        uint8_t has_esr;
        uint64_t esr;
    } serror;

    uint8_t ext_dabt_raised; /* Tracking/verifying injection of ext DABT */

    /* State of our input IRQ/FIQ/VIRQ/VFIQ lines */
    uint32_t irq_line_state;

    /* Thumb-2 EE state.  */
    uint32_t teecr;
    uint32_t teehbr;

    /* VFP coprocessor state.  */
    struct {
        ARMVectorReg zregs[32];

#ifdef TARGET_AARCH64
        /* Store FFR as pregs[16] to make it easier to treat as any other.  */
#define FFR_PRED_NUM 16
        ARMPredicateReg pregs[17];
        /* Scratch space for aa64 sve predicate temporary.  */
        ARMPredicateReg preg_tmp;
#endif

        /* We store these fpcsr fields separately for convenience.  */
        uint32_t qc[4] QEMU_ALIGNED(16);
        int vec_len;
        int vec_stride;

        uint32_t xregs[16];

        /* Scratch space for aa32 neon expansion.  */
        uint32_t scratch[8];

        /* There are a number of distinct float control structures:
         *
         *  fp_status: is the "normal" fp status.
         *  fp_status_fp16: used for half-precision calculations
         *  standard_fp_status : the ARM "Standard FPSCR Value"
         *  standard_fp_status_fp16 : used for half-precision
         *       calculations with the ARM "Standard FPSCR Value"
         *
         * Half-precision operations are governed by a separate
         * flush-to-zero control bit in FPSCR:FZ16. We pass a separate
         * status structure to control this.
         *
         * The "Standard FPSCR", ie default-NaN, flush-to-zero,
         * round-to-nearest and is used by any operations (generally
         * Neon) which the architecture defines as controlled by the
         * standard FPSCR value rather than the FPSCR.
         *
         * The "standard FPSCR but for fp16 ops" is needed because
         * the "standard FPSCR" tracks the FPSCR.FZ16 bit rather than
         * using a fixed value for it.
         *
         * To avoid having to transfer exception bits around, we simply
         * say that the FPSCR cumulative exception flags are the logical
         * OR of the flags in the four fp statuses. This relies on the
         * only thing which needs to read the exception flags being
         * an explicit FPSCR read.
         */
        float_status fp_status;
        float_status fp_status_f16;
        float_status standard_fp_status;
        float_status standard_fp_status_f16;

        /* ZCR_EL[1-3] */
        uint64_t zcr_el[4];
    } vfp;
    uint64_t exclusive_addr;
    uint64_t exclusive_val;
    uint64_t exclusive_high;

    /* iwMMXt coprocessor state.  */
    struct {
        uint64_t regs[16];
        uint64_t val;

        uint32_t cregs[16];
    } iwmmxt;

#ifdef TARGET_AARCH64
    struct {
        ARMPACKey apia;
        ARMPACKey apib;
        ARMPACKey apda;
        ARMPACKey apdb;
        ARMPACKey apga;
    } keys;
#endif

#if defined(CONFIG_USER_ONLY)
    /* For usermode syscall translation.  */
    int eabi;
#endif

    struct CPUBreakpoint *cpu_breakpoint[16];
    struct CPUWatchpoint *cpu_watchpoint[16];

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields after this point are preserved across CPU reset. */

    /* Internal CPU feature flags.  */
    uint64_t features;

    /* PMSAv7 MPU */
    struct {
        uint32_t *drbar;
        uint32_t *drsr;
        uint32_t *dracr;
        uint32_t rnr[M_REG_NUM_BANKS];
    } pmsav7;

    /* PMSAv8 MPU */
    struct {
        /* The PMSAv8 implementation also shares some PMSAv7 config
         * and state:
         *  pmsav7.rnr (region number register)
         *  pmsav7_dregion (number of configured regions)
         */
        uint32_t *rbar[M_REG_NUM_BANKS];
        uint32_t *rlar[M_REG_NUM_BANKS];
        uint32_t mair0[M_REG_NUM_BANKS];
        uint32_t mair1[M_REG_NUM_BANKS];
    } pmsav8;

    /* v8M SAU */
    struct {
        uint32_t *rbar;
        uint32_t *rlar;
        uint32_t rnr;
        uint32_t ctrl;
    } sau;

    void *nvic;
    const struct arm_boot_info *boot_info;
    /* Store GICv3CPUState to access from this struct */
    void *gicv3state;

#ifdef TARGET_TAGGED_ADDRESSES
    /* Linux syscall tagged address support */
    bool tagged_addr_enable;
#endif
} CPUARMState;

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static inline void unset_feature(CPUARMState *env, int feature)
{
    env->features &= ~(1ULL << feature);
}

/**
 * ARMELChangeHookFn:
 * type of a function which can be registered via arm_register_el_change_hook()
 * to get callbacks when the CPU changes its exception level or mode.
 */
typedef void ARMELChangeHookFn(ARMCPU *cpu, void *opaque);
typedef struct ARMELChangeHook ARMELChangeHook;
struct ARMELChangeHook {
    ARMELChangeHookFn *hook;
    void *opaque;
    QLIST_ENTRY(ARMELChangeHook) node;
};

/* These values map onto the return values for
 * QEMU_PSCI_0_2_FN_AFFINITY_INFO */
typedef enum ARMPSCIState {
    PSCI_ON = 0,
    PSCI_OFF = 1,
    PSCI_ON_PENDING = 2
} ARMPSCIState;

typedef struct ARMISARegisters ARMISARegisters;

/**
 * ARMCPU:
 * @env: #CPUARMState
 *
 * An ARM CPU core.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUARMState env;

    /* Coprocessor information */
    GHashTable *cp_regs;
    /* For marshalling (mostly coprocessor) register state between the
     * kernel and QEMU (for KVM) and between two QEMUs (for migration),
     * we use these arrays.
     */
    /* List of register indexes managed via these arrays; (full KVM style
     * 64 bit indexes, not CPRegInfo 32 bit indexes)
     */
    uint64_t *cpreg_indexes;
    /* Values of the registers (cpreg_indexes[i]'s value is cpreg_values[i]) */
    uint64_t *cpreg_values;
    /* Length of the indexes, values, reset_values arrays */
    int32_t cpreg_array_len;
    /* These are used only for migration: incoming data arrives in
     * these fields and is sanity checked in post_load before copying
     * to the working data structures above.
     */
    uint64_t *cpreg_vmstate_indexes;
    uint64_t *cpreg_vmstate_values;
    int32_t cpreg_vmstate_array_len;

    DynamicGDBXMLInfo dyn_sysreg_xml;
    DynamicGDBXMLInfo dyn_svereg_xml;

    /* Timers used by the generic (architected) timer */
    QEMUTimer *gt_timer[NUM_GTIMERS];
    /*
     * Timer used by the PMU. Its state is restored after migration by
     * pmu_op_finish() - it does not need other handling during migration
     */
    QEMUTimer *pmu_timer;
    /* GPIO outputs for generic timer */
    qemu_irq gt_timer_outputs[NUM_GTIMERS];
    /* GPIO output for GICv3 maintenance interrupt signal */
    qemu_irq gicv3_maintenance_interrupt;
    /* GPIO output for the PMU interrupt */
    qemu_irq pmu_interrupt;

    /* MemoryRegion to use for secure physical accesses */
    MemoryRegion *secure_memory;

    /* MemoryRegion to use for allocation tag accesses */
    MemoryRegion *tag_memory;
    MemoryRegion *secure_tag_memory;

    /* For v8M, pointer to the IDAU interface provided by board/SoC */
    Object *idau;

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;

    /* PSCI version for this CPU
     * Bits[31:16] = Major Version
     * Bits[15:0] = Minor Version
     */
    uint32_t psci_version;

    /* Current power state, access guarded by BQL */
    ARMPSCIState power_state;

    /* CPU has virtualization extension */
    bool has_el2;
    /* CPU has security extension */
    bool has_el3;
    /* CPU has PMU (Performance Monitor Unit) */
    bool has_pmu;
    /* CPU has VFP */
    bool has_vfp;
    /* CPU has Neon */
    bool has_neon;
    /* CPU has M-profile DSP extension */
    bool has_dsp;

    /* CPU has memory protection unit */
    bool has_mpu;
    /* PMSAv7 MPU number of supported regions */
    uint32_t pmsav7_dregion;
    /* v8M SAU number of supported regions */
    uint32_t sau_sregion;

    /* PSCI conduit used to invoke PSCI methods
     * 0 - disabled, 1 - smc, 2 - hvc
     */
    uint32_t psci_conduit;

    /* For v8M, initial value of the Secure VTOR */
    uint32_t init_svtor;
    /* For v8M, initial value of the Non-secure VTOR */
    uint32_t init_nsvtor;

    /* [QEMU_]KVM_ARM_TARGET_* constant for this CPU, or
     * QEMU_KVM_ARM_TARGET_NONE if the kernel doesn't support this CPU type.
     */
    uint32_t kvm_target;

    /* KVM init features for this CPU */
    uint32_t kvm_init_features[7];

    /* KVM CPU state */

    /* KVM virtual time adjustment */
    bool kvm_adjvtime;
    bool kvm_vtime_dirty;
    uint64_t kvm_vtime;

    /* KVM steal time */
    OnOffAuto kvm_steal_time;

    /* Uniprocessor system with MP extensions */
    bool mp_is_up;

    /* True if we tried kvm_arm_host_cpu_features() during CPU instance_init
     * and the probe failed (so we need to report the error in realize)
     */
    bool host_cpu_probe_failed;

    /* Specify the number of cores in this CPU cluster. Used for the L2CTLR
     * register.
     */
    int32_t core_count;

    /* The instance init functions for implementation-specific subclasses
     * set these fields to specify the implementation-dependent values of
     * various constant registers and reset values of non-constant
     * registers.
     * Some of these might become QOM properties eventually.
     * Field names match the official register names as defined in the
     * ARMv7AR ARM Architecture Reference Manual. A reset_ prefix
     * is used for reset values of non-constant registers; no reset_
     * prefix means a constant register.
     * Some of these registers are split out into a substructure that
     * is shared with the translators to control the ISA.
     *
     * Note that if you add an ID register to the ARMISARegisters struct
     * you need to also update the 32-bit and 64-bit versions of the
     * kvm_arm_get_host_cpu_features() function to correctly populate the
     * field by reading the value from the KVM vCPU.
     */
    struct ARMISARegisters {
        uint32_t id_isar0;
        uint32_t id_isar1;
        uint32_t id_isar2;
        uint32_t id_isar3;
        uint32_t id_isar4;
        uint32_t id_isar5;
        uint32_t id_isar6;
        uint32_t id_mmfr0;
        uint32_t id_mmfr1;
        uint32_t id_mmfr2;
        uint32_t id_mmfr3;
        uint32_t id_mmfr4;
        uint32_t id_pfr0;
        uint32_t id_pfr1;
        uint32_t id_pfr2;
        uint32_t mvfr0;
        uint32_t mvfr1;
        uint32_t mvfr2;
        uint32_t id_dfr0;
        uint32_t dbgdidr;
        uint64_t id_aa64isar0;
        uint64_t id_aa64isar1;
        uint64_t id_aa64pfr0;
        uint64_t id_aa64pfr1;
        uint64_t id_aa64mmfr0;
        uint64_t id_aa64mmfr1;
        uint64_t id_aa64mmfr2;
        uint64_t id_aa64dfr0;
        uint64_t id_aa64dfr1;
        uint64_t id_aa64zfr0;
    } isar;
    uint64_t midr;
    uint32_t revidr;
    uint32_t reset_fpsid;
    uint64_t ctr;
    uint32_t reset_sctlr;
    uint64_t pmceid0;
    uint64_t pmceid1;
    uint32_t id_afr0;
    uint64_t id_aa64afr0;
    uint64_t id_aa64afr1;
    uint64_t clidr;
    uint64_t mp_affinity; /* MP ID without feature bits */
    /* The elements of this array are the CCSIDR values for each cache,
     * in the order L1DCache, L1ICache, L2DCache, L2ICache, etc.
     */
    uint64_t ccsidr[16];
    uint64_t reset_cbar;
    uint32_t reset_auxcr;
    bool reset_hivecs;

    /*
     * Intermediate values used during property parsing.
     * Once finalized, the values should be read from ID_AA64*.
     */
    bool prop_pauth;
    bool prop_pauth_impdef;
    bool prop_lpa2;

    /* DCZ blocksize, in log_2(words), ie low 4 bits of DCZID_EL0 */
    uint32_t dcz_blocksize;
    uint64_t rvbar_prop; /* Property/input signals.  */

    /* Configurable aspects of GIC cpu interface (which is part of the CPU) */
    int gic_num_lrs; /* number of list registers */
    int gic_vpribits; /* number of virtual priority bits */
    int gic_vprebits; /* number of virtual preemption bits */

    /* Whether the cfgend input is high (i.e. this CPU should reset into
     * big-endian mode).  This setting isn't used directly: instead it modifies
     * the reset_sctlr value to have SCTLR_B or SCTLR_EE set, depending on the
     * architecture version.
     */
    bool cfgend;

    QLIST_HEAD(, ARMELChangeHook) pre_el_change_hooks;
    QLIST_HEAD(, ARMELChangeHook) el_change_hooks;

    int32_t node_id; /* NUMA node this CPU belongs to */

    /* Used to synchronize KVM and QEMU in-kernel device levels */
    uint8_t device_irq_level;

    /* Used to set the maximum vector length the cpu will support.  */
    uint32_t sve_max_vq;

#ifdef CONFIG_USER_ONLY
    /* Used to set the default vector length at process start. */
    uint32_t sve_default_vq;
#endif

    /*
     * In sve_vq_map each set bit is a supported vector length of
     * (bit-number + 1) * 16 bytes, i.e. each bit number + 1 is the vector
     * length in quadwords.
     *
     * While processing properties during initialization, corresponding
     * sve_vq_init bits are set for bits in sve_vq_map that have been
     * set by properties.
     *
     * Bits set in sve_vq_supported represent valid vector lengths for
     * the CPU type.
     */
    DECLARE_BITMAP(sve_vq_map, ARM_MAX_VQ);
    DECLARE_BITMAP(sve_vq_init, ARM_MAX_VQ);
    DECLARE_BITMAP(sve_vq_supported, ARM_MAX_VQ);

    /* Generic timer counter frequency, in Hz */
    uint64_t gt_cntfrq_hz;
};

unsigned int gt_cntfrq_period_ns(ARMCPU *cpu);

void arm_cpu_post_init(Object *obj);

uint64_t arm_cpu_mp_affinity(int idx, uint8_t clustersz);

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_arm_cpu;

void arm_cpu_do_interrupt(CPUState *cpu);
void arm_v7m_cpu_do_interrupt(CPUState *cpu);
#endif /* !CONFIG_USER_ONLY */

hwaddr arm_cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                         MemTxAttrs *attrs);

int arm_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int arm_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

/*
 * Helpers to dynamically generates XML descriptions of the sysregs
 * and SVE registers. Returns the number of registers in each set.
 */
int arm_gen_dynamic_sysreg_xml(CPUState *cpu, int base_reg);
int arm_gen_dynamic_svereg_xml(CPUState *cpu, int base_reg);

/* Returns the dynamically generated XML for the gdb stub.
 * Returns a pointer to the XML contents for the specified XML file or NULL
 * if the XML name doesn't match the predefined one.
 */
const char *arm_gdb_get_dynamic_xml(CPUState *cpu, const char *xmlname);

int arm_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, void *opaque);
int arm_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, void *opaque);

#ifdef TARGET_AARCH64
int aarch64_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int aarch64_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq);
void aarch64_sve_change_el(CPUARMState *env, int old_el,
                           int new_el, bool el0_a64);
void aarch64_add_sve_properties(Object *obj);
void aarch64_add_pauth_properties(Object *obj);

/*
 * SVE registers are encoded in KVM's memory in an endianness-invariant format.
 * The byte at offset i from the start of the in-memory representation contains
 * the bits [(7 + 8 * i) : (8 * i)] of the register value. As this means the
 * lowest offsets are stored in the lowest memory addresses, then that nearly
 * matches QEMU's representation, which is to use an array of host-endian
 * uint64_t's, where the lower offsets are at the lower indices. To complete
 * the translation we just need to byte swap the uint64_t's on big-endian hosts.
 */
static inline uint64_t *sve_bswap64(uint64_t *dst, uint64_t *src, int nr)
{
#ifdef HOST_WORDS_BIGENDIAN
    int i;

    for (i = 0; i < nr; ++i) {
        dst[i] = bswap64(src[i]);
    }

    return dst;
#else
    return src;
#endif
}

#else
static inline void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq) { }
static inline void aarch64_sve_change_el(CPUARMState *env, int o,
                                         int n, bool a)
{ }
static inline void aarch64_add_sve_properties(Object *obj) { }
#endif

void aarch64_sync_32_to_64(CPUARMState *env);
void aarch64_sync_64_to_32(CPUARMState *env);

int fp_exception_el(CPUARMState *env, int cur_el);
int sve_exception_el(CPUARMState *env, int cur_el);
uint32_t sve_zcr_len_for_el(CPUARMState *env, int el);

static inline bool is_a64(CPUARMState *env)
{
    return env->aarch64;
}

/**
 * pmu_op_start/finish
 * @env: CPUARMState
 *
 * Convert all PMU counters between their delta form (the typical mode when
 * they are enabled) and the guest-visible values. These two calls must
 * surround any action which might affect the counters.
 */
void pmu_op_start(CPUARMState *env);
void pmu_op_finish(CPUARMState *env);

/*
 * Called when a PMU counter is due to overflow
 */
void arm_pmu_timer_cb(void *opaque);

/**
 * Functions to register as EL change hooks for PMU mode filtering
 */
void pmu_pre_el_change(ARMCPU *cpu, void *ignored);
void pmu_post_el_change(ARMCPU *cpu, void *ignored);

/*
 * pmu_init
 * @cpu: ARMCPU
 *
 * Initialize the CPU's PMCEID[01]_EL0 registers and associated internal state
 * for the current configuration
 */
void pmu_init(ARMCPU *cpu);

/* SCTLR bit meanings. Several bits have been reused in newer
 * versions of the architecture; in that case we define constants
 * for both old and new bit meanings. Code which tests against those
 * bits should probably check or otherwise arrange that the CPU
 * is the architectural version it expects.
 */
#define SCTLR_M       (1U << 0)
#define SCTLR_A       (1U << 1)
#define SCTLR_C       (1U << 2)
#define SCTLR_W       (1U << 3) /* up to v6; RAO in v7 */
#define SCTLR_nTLSMD_32 (1U << 3) /* v8.2-LSMAOC, AArch32 only */
#define SCTLR_SA      (1U << 3) /* AArch64 only */
#define SCTLR_P       (1U << 4) /* up to v5; RAO in v6 and v7 */
#define SCTLR_LSMAOE_32 (1U << 4) /* v8.2-LSMAOC, AArch32 only */
#define SCTLR_SA0     (1U << 4) /* v8 onward, AArch64 only */
#define SCTLR_D       (1U << 5) /* up to v5; RAO in v6 */
#define SCTLR_CP15BEN (1U << 5) /* v7 onward */
#define SCTLR_L       (1U << 6) /* up to v5; RAO in v6 and v7; RAZ in v8 */
#define SCTLR_nAA     (1U << 6) /* when v8.4-LSE is implemented */
#define SCTLR_B       (1U << 7) /* up to v6; RAZ in v7 */
#define SCTLR_ITD     (1U << 7) /* v8 onward */
#define SCTLR_S       (1U << 8) /* up to v6; RAZ in v7 */
#define SCTLR_SED     (1U << 8) /* v8 onward */
#define SCTLR_R       (1U << 9) /* up to v6; RAZ in v7 */
#define SCTLR_UMA     (1U << 9) /* v8 onward, AArch64 only */
#define SCTLR_F       (1U << 10) /* up to v6 */
#define SCTLR_SW      (1U << 10) /* v7 */
#define SCTLR_EnRCTX  (1U << 10) /* in v8.0-PredInv */
#define SCTLR_Z       (1U << 11) /* in v7, RES1 in v8 */
#define SCTLR_EOS     (1U << 11) /* v8.5-ExS */
#define SCTLR_I       (1U << 12)
#define SCTLR_V       (1U << 13) /* AArch32 only */
#define SCTLR_EnDB    (1U << 13) /* v8.3, AArch64 only */
#define SCTLR_RR      (1U << 14) /* up to v7 */
#define SCTLR_DZE     (1U << 14) /* v8 onward, AArch64 only */
#define SCTLR_L4      (1U << 15) /* up to v6; RAZ in v7 */
#define SCTLR_UCT     (1U << 15) /* v8 onward, AArch64 only */
#define SCTLR_DT      (1U << 16) /* up to ??, RAO in v6 and v7 */
#define SCTLR_nTWI    (1U << 16) /* v8 onward */
#define SCTLR_HA      (1U << 17) /* up to v7, RES0 in v8 */
#define SCTLR_BR      (1U << 17) /* PMSA only */
#define SCTLR_IT      (1U << 18) /* up to ??, RAO in v6 and v7 */
#define SCTLR_nTWE    (1U << 18) /* v8 onward */
#define SCTLR_WXN     (1U << 19)
#define SCTLR_ST      (1U << 20) /* up to ??, RAZ in v6 */
#define SCTLR_UWXN    (1U << 20) /* v7 onward, AArch32 only */
#define SCTLR_FI      (1U << 21) /* up to v7, v8 RES0 */
#define SCTLR_IESB    (1U << 21) /* v8.2-IESB, AArch64 only */
#define SCTLR_U       (1U << 22) /* up to v6, RAO in v7 */
#define SCTLR_EIS     (1U << 22) /* v8.5-ExS */
#define SCTLR_XP      (1U << 23) /* up to v6; v7 onward RAO */
#define SCTLR_SPAN    (1U << 23) /* v8.1-PAN */
#define SCTLR_VE      (1U << 24) /* up to v7 */
#define SCTLR_E0E     (1U << 24) /* v8 onward, AArch64 only */
#define SCTLR_EE      (1U << 25)
#define SCTLR_L2      (1U << 26) /* up to v6, RAZ in v7 */
#define SCTLR_UCI     (1U << 26) /* v8 onward, AArch64 only */
#define SCTLR_NMFI    (1U << 27) /* up to v7, RAZ in v7VE and v8 */
#define SCTLR_EnDA    (1U << 27) /* v8.3, AArch64 only */
#define SCTLR_TRE     (1U << 28) /* AArch32 only */
#define SCTLR_nTLSMD_64 (1U << 28) /* v8.2-LSMAOC, AArch64 only */
#define SCTLR_AFE     (1U << 29) /* AArch32 only */
#define SCTLR_LSMAOE_64 (1U << 29) /* v8.2-LSMAOC, AArch64 only */
#define SCTLR_TE      (1U << 30) /* AArch32 only */
#define SCTLR_EnIB    (1U << 30) /* v8.3, AArch64 only */
#define SCTLR_EnIA    (1U << 31) /* v8.3, AArch64 only */
#define SCTLR_DSSBS_32 (1U << 31) /* v8.5, AArch32 only */
#define SCTLR_BT0     (1ULL << 35) /* v8.5-BTI */
#define SCTLR_BT1     (1ULL << 36) /* v8.5-BTI */
#define SCTLR_ITFSB   (1ULL << 37) /* v8.5-MemTag */
#define SCTLR_TCF0    (3ULL << 38) /* v8.5-MemTag */
#define SCTLR_TCF     (3ULL << 40) /* v8.5-MemTag */
#define SCTLR_ATA0    (1ULL << 42) /* v8.5-MemTag */
#define SCTLR_ATA     (1ULL << 43) /* v8.5-MemTag */
#define SCTLR_DSSBS_64 (1ULL << 44) /* v8.5, AArch64 only */

#define CPTR_TCPAC    (1U << 31)
#define CPTR_TTA      (1U << 20)
#define CPTR_TFP      (1U << 10)
#define CPTR_TZ       (1U << 8)   /* CPTR_EL2 */
#define CPTR_EZ       (1U << 8)   /* CPTR_EL3 */

#define MDCR_EPMAD    (1U << 21)
#define MDCR_EDAD     (1U << 20)
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
#define SDCR_VALID_MASK (MDCR_EPMAD | MDCR_EDAD | MDCR_SPME | MDCR_SPD)

#define CPSR_M (0x1fU)
#define CPSR_T (1U << 5)
#define CPSR_F (1U << 6)
#define CPSR_I (1U << 7)
#define CPSR_A (1U << 8)
#define CPSR_E (1U << 9)
#define CPSR_IT_2_7 (0xfc00U)
#define CPSR_GE (0xfU << 16)
#define CPSR_IL (1U << 20)
#define CPSR_DIT (1U << 21)
#define CPSR_PAN (1U << 22)
#define CPSR_SSBS (1U << 23)
#define CPSR_J (1U << 24)
#define CPSR_IT_0_1 (3U << 25)
#define CPSR_Q (1U << 27)
#define CPSR_V (1U << 28)
#define CPSR_C (1U << 29)
#define CPSR_Z (1U << 30)
#define CPSR_N (1U << 31)
#define CPSR_NZCV (CPSR_N | CPSR_Z | CPSR_C | CPSR_V)
#define CPSR_AIF (CPSR_A | CPSR_I | CPSR_F)

#define CPSR_IT (CPSR_IT_0_1 | CPSR_IT_2_7)
#define CACHED_CPSR_BITS (CPSR_T | CPSR_AIF | CPSR_GE | CPSR_IT | CPSR_Q \
    | CPSR_NZCV)
/* Bits writable in user mode.  */
#define CPSR_USER (CPSR_NZCV | CPSR_Q | CPSR_GE | CPSR_E)
/* Execution state bits.  MRS read as zero, MSR writes ignored.  */
#define CPSR_EXEC (CPSR_T | CPSR_IT | CPSR_J | CPSR_IL)

/* Bit definitions for M profile XPSR. Most are the same as CPSR. */
#define XPSR_EXCP 0x1ffU
#define XPSR_SPREALIGN (1U << 9) /* Only set in exception stack frames */
#define XPSR_IT_2_7 CPSR_IT_2_7
#define XPSR_GE CPSR_GE
#define XPSR_SFPA (1U << 20) /* Only set in exception stack frames */
#define XPSR_T (1U << 24) /* Not the same as CPSR_T ! */
#define XPSR_IT_0_1 CPSR_IT_0_1
#define XPSR_Q CPSR_Q
#define XPSR_V CPSR_V
#define XPSR_C CPSR_C
#define XPSR_Z CPSR_Z
#define XPSR_N CPSR_N
#define XPSR_NZCV CPSR_NZCV
#define XPSR_IT CPSR_IT

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

/* Bit definitions for ARMv8 SPSR (PSTATE) format.
 * Only these are valid when in AArch64 mode; in
 * AArch32 mode SPSRs are basically CPSR-format.
 */
#define PSTATE_SP (1U)
#define PSTATE_M (0xFU)
#define PSTATE_nRW (1U << 4)
#define PSTATE_F (1U << 6)
#define PSTATE_I (1U << 7)
#define PSTATE_A (1U << 8)
#define PSTATE_D (1U << 9)
#define PSTATE_BTYPE (3U << 10)
#define PSTATE_SSBS (1U << 12)
#define PSTATE_IL (1U << 20)
#define PSTATE_SS (1U << 21)
#define PSTATE_PAN (1U << 22)
#define PSTATE_UAO (1U << 23)
#define PSTATE_DIT (1U << 24)
#define PSTATE_TCO (1U << 25)
#define PSTATE_V (1U << 28)
#define PSTATE_C (1U << 29)
#define PSTATE_Z (1U << 30)
#define PSTATE_N (1U << 31)
#define PSTATE_NZCV (PSTATE_N | PSTATE_Z | PSTATE_C | PSTATE_V)
#define PSTATE_DAIF (PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F)
#define CACHED_PSTATE_BITS (PSTATE_NZCV | PSTATE_DAIF | PSTATE_BTYPE)
/* Mode values for AArch64 */
#define PSTATE_MODE_EL3h 13
#define PSTATE_MODE_EL3t 12
#define PSTATE_MODE_EL2h 9
#define PSTATE_MODE_EL2t 8
#define PSTATE_MODE_EL1h 5
#define PSTATE_MODE_EL1t 4
#define PSTATE_MODE_EL0t 0

/* Write a new value to v7m.exception, thus transitioning into or out
 * of Handler mode; this may result in a change of active stack pointer.
 */
void write_v7m_exception(CPUARMState *env, uint32_t new_exc);

/* Map EL and handler into a PSTATE_MODE.  */
static inline unsigned int aarch64_pstate_mode(unsigned int el, bool handler)
{
    return (el << 2) | handler;
}

/* Return the current PSTATE value. For the moment we don't support 32<->64 bit
 * interprocessing, so we don't attempt to sync with the cpsr state used by
 * the 32 bit decoder.
 */
static inline uint32_t pstate_read(CPUARMState *env)
{
    int ZF;

    ZF = (env->ZF == 0);
    return (env->NF & 0x80000000) | (ZF << 30)
        | (env->CF << 29) | ((env->VF & 0x80000000) >> 3)
        | env->pstate | env->daif | (env->btype << 10);
}

static inline void pstate_write(CPUARMState *env, uint32_t val)
{
    env->ZF = (~val) & PSTATE_Z;
    env->NF = val;
    env->CF = (val >> 29) & 1;
    env->VF = (val << 3) & 0x80000000;
    env->daif = val & PSTATE_DAIF;
    env->btype = (val >> 10) & 3;
    env->pstate = val & ~CACHED_PSTATE_BITS;
}

/* Return the current CPSR value.  */
uint32_t cpsr_read(CPUARMState *env);

typedef enum CPSRWriteType {
    CPSRWriteByInstr = 0,         /* from guest MSR or CPS */
    CPSRWriteExceptionReturn = 1, /* from guest exception return insn */
    CPSRWriteRaw = 2,
        /* trust values, no reg bank switch, no hflags rebuild */
    CPSRWriteByGDBStub = 3,       /* from the GDB stub */
} CPSRWriteType;

/*
 * Set the CPSR.  Note that some bits of mask must be all-set or all-clear.
 * This will do an arm_rebuild_hflags() if any of the bits in @mask
 * correspond to TB flags bits cached in the hflags, unless @write_type
 * is CPSRWriteRaw.
 */
void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask,
                CPSRWriteType write_type);

/* Return the current xPSR value.  */
static inline uint32_t xpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return (env->NF & 0x80000000) | (ZF << 30)
        | (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 24) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | (env->GE << 16)
        | env->v7m.exception;
}

/* Set the xPSR.  Note that some bits of mask must be all-set or all-clear.  */
static inline void xpsr_write(CPUARMState *env, uint32_t val, uint32_t mask)
{
    if (mask & XPSR_NZCV) {
        env->ZF = (~val) & XPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & XPSR_Q) {
        env->QF = ((val & XPSR_Q) != 0);
    }
    if (mask & XPSR_GE) {
        env->GE = (val & XPSR_GE) >> 16;
    }
#ifndef CONFIG_USER_ONLY
    if (mask & XPSR_T) {
        env->thumb = ((val & XPSR_T) != 0);
    }
    if (mask & XPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & XPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & XPSR_EXCP) {
        /* Note that this only happens on exception exit */
        write_v7m_exception(env, val & XPSR_EXCP);
    }
#endif
}

#define HCR_VM        (1ULL << 0)
#define HCR_SWIO      (1ULL << 1)
#define HCR_PTW       (1ULL << 2)
#define HCR_FMO       (1ULL << 3)
#define HCR_IMO       (1ULL << 4)
#define HCR_AMO       (1ULL << 5)
#define HCR_VF        (1ULL << 6)
#define HCR_VI        (1ULL << 7)
#define HCR_VSE       (1ULL << 8)
#define HCR_FB        (1ULL << 9)
#define HCR_BSU_MASK  (3ULL << 10)
#define HCR_DC        (1ULL << 12)
#define HCR_TWI       (1ULL << 13)
#define HCR_TWE       (1ULL << 14)
#define HCR_TID0      (1ULL << 15)
#define HCR_TID1      (1ULL << 16)
#define HCR_TID2      (1ULL << 17)
#define HCR_TID3      (1ULL << 18)
#define HCR_TSC       (1ULL << 19)
#define HCR_TIDCP     (1ULL << 20)
#define HCR_TACR      (1ULL << 21)
#define HCR_TSW       (1ULL << 22)
#define HCR_TPCP      (1ULL << 23)
#define HCR_TPU       (1ULL << 24)
#define HCR_TTLB      (1ULL << 25)
#define HCR_TVM       (1ULL << 26)
#define HCR_TGE       (1ULL << 27)
#define HCR_TDZ       (1ULL << 28)
#define HCR_HCD       (1ULL << 29)
#define HCR_TRVM      (1ULL << 30)
#define HCR_RW        (1ULL << 31)
#define HCR_CD        (1ULL << 32)
#define HCR_ID        (1ULL << 33)
#define HCR_E2H       (1ULL << 34)
#define HCR_TLOR      (1ULL << 35)
#define HCR_TERR      (1ULL << 36)
#define HCR_TEA       (1ULL << 37)
#define HCR_MIOCNCE   (1ULL << 38)
/* RES0 bit 39 */
#define HCR_APK       (1ULL << 40)
#define HCR_API       (1ULL << 41)
#define HCR_NV        (1ULL << 42)
#define HCR_NV1       (1ULL << 43)
#define HCR_AT        (1ULL << 44)
#define HCR_NV2       (1ULL << 45)
#define HCR_FWB       (1ULL << 46)
#define HCR_FIEN      (1ULL << 47)
/* RES0 bit 48 */
#define HCR_TID4      (1ULL << 49)
#define HCR_TICAB     (1ULL << 50)
#define HCR_AMVOFFEN  (1ULL << 51)
#define HCR_TOCU      (1ULL << 52)
#define HCR_ENSCXT    (1ULL << 53)
#define HCR_TTLBIS    (1ULL << 54)
#define HCR_TTLBOS    (1ULL << 55)
#define HCR_ATA       (1ULL << 56)
#define HCR_DCT       (1ULL << 57)
#define HCR_TID5      (1ULL << 58)
#define HCR_TWEDEN    (1ULL << 59)
#define HCR_TWEDEL    MAKE_64BIT_MASK(60, 4)

#define HPFAR_NS      (1ULL << 63)

#define SCR_NS                (1U << 0)
#define SCR_IRQ               (1U << 1)
#define SCR_FIQ               (1U << 2)
#define SCR_EA                (1U << 3)
#define SCR_FW                (1U << 4)
#define SCR_AW                (1U << 5)
#define SCR_NET               (1U << 6)
#define SCR_SMD               (1U << 7)
#define SCR_HCE               (1U << 8)
#define SCR_SIF               (1U << 9)
#define SCR_RW                (1U << 10)
#define SCR_ST                (1U << 11)
#define SCR_TWI               (1U << 12)
#define SCR_TWE               (1U << 13)
#define SCR_TLOR              (1U << 14)
#define SCR_TERR              (1U << 15)
#define SCR_APK               (1U << 16)
#define SCR_API               (1U << 17)
#define SCR_EEL2              (1U << 18)
#define SCR_EASE              (1U << 19)
#define SCR_NMEA              (1U << 20)
#define SCR_FIEN              (1U << 21)
#define SCR_ENSCXT            (1U << 25)
#define SCR_ATA               (1U << 26)

#define HSTR_TTEE (1 << 16)
#define HSTR_TJDBX (1 << 17)

/* Return the current FPSCR value.  */
uint32_t vfp_get_fpscr(CPUARMState *env);
void vfp_set_fpscr(CPUARMState *env, uint32_t val);

/* FPCR, Floating Point Control Register
 * FPSR, Floating Poiht Status Register
 *
 * For A64 the FPSCR is split into two logically distinct registers,
 * FPCR and FPSR. However since they still use non-overlapping bits
 * we store the underlying state in fpscr and just mask on read/write.
 */
#define FPSR_MASK 0xf800009f
#define FPCR_MASK 0x07ff9f00

#define FPCR_IOE    (1 << 8)    /* Invalid Operation exception trap enable */
#define FPCR_DZE    (1 << 9)    /* Divide by Zero exception trap enable */
#define FPCR_OFE    (1 << 10)   /* Overflow exception trap enable */
#define FPCR_UFE    (1 << 11)   /* Underflow exception trap enable */
#define FPCR_IXE    (1 << 12)   /* Inexact exception trap enable */
#define FPCR_IDE    (1 << 15)   /* Input Denormal exception trap enable */
#define FPCR_FZ16   (1 << 19)   /* ARMv8.2+, FP16 flush-to-zero */
#define FPCR_RMODE_MASK (3 << 22) /* Rounding mode */
#define FPCR_FZ     (1 << 24)   /* Flush-to-zero enable bit */
#define FPCR_DN     (1 << 25)   /* Default NaN enable bit */
#define FPCR_AHP    (1 << 26)   /* Alternative half-precision */
#define FPCR_QC     (1 << 27)   /* Cumulative saturation bit */
#define FPCR_V      (1 << 28)   /* FP overflow flag */
#define FPCR_C      (1 << 29)   /* FP carry flag */
#define FPCR_Z      (1 << 30)   /* FP zero flag */
#define FPCR_N      (1 << 31)   /* FP negative flag */

#define FPCR_LTPSIZE_SHIFT 16   /* LTPSIZE, M-profile only */
#define FPCR_LTPSIZE_MASK (7 << FPCR_LTPSIZE_SHIFT)
#define FPCR_LTPSIZE_LENGTH 3

#define FPCR_NZCV_MASK (FPCR_N | FPCR_Z | FPCR_C | FPCR_V)
#define FPCR_NZCVQC_MASK (FPCR_NZCV_MASK | FPCR_QC)

static inline uint32_t vfp_get_fpsr(CPUARMState *env)
{
    return vfp_get_fpscr(env) & FPSR_MASK;
}

static inline void vfp_set_fpsr(CPUARMState *env, uint32_t val)
{
    uint32_t new_fpscr = (vfp_get_fpscr(env) & ~FPSR_MASK) | (val & FPSR_MASK);
    vfp_set_fpscr(env, new_fpscr);
}

static inline uint32_t vfp_get_fpcr(CPUARMState *env)
{
    return vfp_get_fpscr(env) & FPCR_MASK;
}

static inline void vfp_set_fpcr(CPUARMState *env, uint32_t val)
{
    uint32_t new_fpscr = (vfp_get_fpscr(env) & ~FPCR_MASK) | (val & FPCR_MASK);
    vfp_set_fpscr(env, new_fpscr);
}

enum arm_cpu_mode {
  ARM_CPU_MODE_USR = 0x10,
  ARM_CPU_MODE_FIQ = 0x11,
  ARM_CPU_MODE_IRQ = 0x12,
  ARM_CPU_MODE_SVC = 0x13,
  ARM_CPU_MODE_MON = 0x16,
  ARM_CPU_MODE_ABT = 0x17,
  ARM_CPU_MODE_HYP = 0x1a,
  ARM_CPU_MODE_UND = 0x1b,
  ARM_CPU_MODE_SYS = 0x1f
};

/* VFP system registers.  */
#define ARM_VFP_FPSID   0
#define ARM_VFP_FPSCR   1
#define ARM_VFP_MVFR2   5
#define ARM_VFP_MVFR1   6
#define ARM_VFP_MVFR0   7
#define ARM_VFP_FPEXC   8
#define ARM_VFP_FPINST  9
#define ARM_VFP_FPINST2 10
/* These ones are M-profile only */
#define ARM_VFP_FPSCR_NZCVQC 2
#define ARM_VFP_VPR 12
#define ARM_VFP_P0 13
#define ARM_VFP_FPCXT_NS 14
#define ARM_VFP_FPCXT_S 15

/* QEMU-internal value meaning "FPSCR, but we care only about NZCV" */
#define QEMU_VFP_FPSCR_NZCV 0xffff

/* iwMMXt coprocessor control registers.  */
#define ARM_IWMMXT_wCID  0
#define ARM_IWMMXT_wCon  1
#define ARM_IWMMXT_wCSSF 2
#define ARM_IWMMXT_wCASF 3
#define ARM_IWMMXT_wCGR0 8
#define ARM_IWMMXT_wCGR1 9
#define ARM_IWMMXT_wCGR2 10
#define ARM_IWMMXT_wCGR3 11

/* V7M CCR bits */
FIELD(V7M_CCR, NONBASETHRDENA, 0, 1)
FIELD(V7M_CCR, USERSETMPEND, 1, 1)
FIELD(V7M_CCR, UNALIGN_TRP, 3, 1)
FIELD(V7M_CCR, DIV_0_TRP, 4, 1)
FIELD(V7M_CCR, BFHFNMIGN, 8, 1)
FIELD(V7M_CCR, STKALIGN, 9, 1)
FIELD(V7M_CCR, STKOFHFNMIGN, 10, 1)
FIELD(V7M_CCR, DC, 16, 1)
FIELD(V7M_CCR, IC, 17, 1)
FIELD(V7M_CCR, BP, 18, 1)
FIELD(V7M_CCR, LOB, 19, 1)
FIELD(V7M_CCR, TRD, 20, 1)

/* V7M SCR bits */
FIELD(V7M_SCR, SLEEPONEXIT, 1, 1)
FIELD(V7M_SCR, SLEEPDEEP, 2, 1)
FIELD(V7M_SCR, SLEEPDEEPS, 3, 1)
FIELD(V7M_SCR, SEVONPEND, 4, 1)

/* V7M AIRCR bits */
FIELD(V7M_AIRCR, VECTRESET, 0, 1)
FIELD(V7M_AIRCR, VECTCLRACTIVE, 1, 1)
FIELD(V7M_AIRCR, SYSRESETREQ, 2, 1)
FIELD(V7M_AIRCR, SYSRESETREQS, 3, 1)
FIELD(V7M_AIRCR, PRIGROUP, 8, 3)
FIELD(V7M_AIRCR, BFHFNMINS, 13, 1)
FIELD(V7M_AIRCR, PRIS, 14, 1)
FIELD(V7M_AIRCR, ENDIANNESS, 15, 1)
FIELD(V7M_AIRCR, VECTKEY, 16, 16)

/* V7M CFSR bits for MMFSR */
FIELD(V7M_CFSR, IACCVIOL, 0, 1)
FIELD(V7M_CFSR, DACCVIOL, 1, 1)
FIELD(V7M_CFSR, MUNSTKERR, 3, 1)
FIELD(V7M_CFSR, MSTKERR, 4, 1)
FIELD(V7M_CFSR, MLSPERR, 5, 1)
FIELD(V7M_CFSR, MMARVALID, 7, 1)

/* V7M CFSR bits for BFSR */
FIELD(V7M_CFSR, IBUSERR, 8 + 0, 1)
FIELD(V7M_CFSR, PRECISERR, 8 + 1, 1)
FIELD(V7M_CFSR, IMPRECISERR, 8 + 2, 1)
FIELD(V7M_CFSR, UNSTKERR, 8 + 3, 1)
FIELD(V7M_CFSR, STKERR, 8 + 4, 1)
FIELD(V7M_CFSR, LSPERR, 8 + 5, 1)
FIELD(V7M_CFSR, BFARVALID, 8 + 7, 1)

/* V7M CFSR bits for UFSR */
FIELD(V7M_CFSR, UNDEFINSTR, 16 + 0, 1)
FIELD(V7M_CFSR, INVSTATE, 16 + 1, 1)
FIELD(V7M_CFSR, INVPC, 16 + 2, 1)
FIELD(V7M_CFSR, NOCP, 16 + 3, 1)
FIELD(V7M_CFSR, STKOF, 16 + 4, 1)
FIELD(V7M_CFSR, UNALIGNED, 16 + 8, 1)
FIELD(V7M_CFSR, DIVBYZERO, 16 + 9, 1)

/* V7M CFSR bit masks covering all of the subregister bits */
FIELD(V7M_CFSR, MMFSR, 0, 8)
FIELD(V7M_CFSR, BFSR, 8, 8)
FIELD(V7M_CFSR, UFSR, 16, 16)

/* V7M HFSR bits */
FIELD(V7M_HFSR, VECTTBL, 1, 1)
FIELD(V7M_HFSR, FORCED, 30, 1)
FIELD(V7M_HFSR, DEBUGEVT, 31, 1)

/* V7M DFSR bits */
FIELD(V7M_DFSR, HALTED, 0, 1)
FIELD(V7M_DFSR, BKPT, 1, 1)
FIELD(V7M_DFSR, DWTTRAP, 2, 1)
FIELD(V7M_DFSR, VCATCH, 3, 1)
FIELD(V7M_DFSR, EXTERNAL, 4, 1)

/* V7M SFSR bits */
FIELD(V7M_SFSR, INVEP, 0, 1)
FIELD(V7M_SFSR, INVIS, 1, 1)
FIELD(V7M_SFSR, INVER, 2, 1)
FIELD(V7M_SFSR, AUVIOL, 3, 1)
FIELD(V7M_SFSR, INVTRAN, 4, 1)
FIELD(V7M_SFSR, LSPERR, 5, 1)
FIELD(V7M_SFSR, SFARVALID, 6, 1)
FIELD(V7M_SFSR, LSERR, 7, 1)

/* v7M MPU_CTRL bits */
FIELD(V7M_MPU_CTRL, ENABLE, 0, 1)
FIELD(V7M_MPU_CTRL, HFNMIENA, 1, 1)
FIELD(V7M_MPU_CTRL, PRIVDEFENA, 2, 1)

/* v7M CLIDR bits */
FIELD(V7M_CLIDR, CTYPE_ALL, 0, 21)
FIELD(V7M_CLIDR, LOUIS, 21, 3)
FIELD(V7M_CLIDR, LOC, 24, 3)
FIELD(V7M_CLIDR, LOUU, 27, 3)
FIELD(V7M_CLIDR, ICB, 30, 2)

FIELD(V7M_CSSELR, IND, 0, 1)
FIELD(V7M_CSSELR, LEVEL, 1, 3)
/* We use the combination of InD and Level to index into cpu->ccsidr[];
 * define a mask for this and check that it doesn't permit running off
 * the end of the array.
 */
FIELD(V7M_CSSELR, INDEX, 0, 4)

/* v7M FPCCR bits */
FIELD(V7M_FPCCR, LSPACT, 0, 1)
FIELD(V7M_FPCCR, USER, 1, 1)
FIELD(V7M_FPCCR, S, 2, 1)
FIELD(V7M_FPCCR, THREAD, 3, 1)
FIELD(V7M_FPCCR, HFRDY, 4, 1)
FIELD(V7M_FPCCR, MMRDY, 5, 1)
FIELD(V7M_FPCCR, BFRDY, 6, 1)
FIELD(V7M_FPCCR, SFRDY, 7, 1)
FIELD(V7M_FPCCR, MONRDY, 8, 1)
FIELD(V7M_FPCCR, SPLIMVIOL, 9, 1)
FIELD(V7M_FPCCR, UFRDY, 10, 1)
FIELD(V7M_FPCCR, RES0, 11, 15)
FIELD(V7M_FPCCR, TS, 26, 1)
FIELD(V7M_FPCCR, CLRONRETS, 27, 1)
FIELD(V7M_FPCCR, CLRONRET, 28, 1)
FIELD(V7M_FPCCR, LSPENS, 29, 1)
FIELD(V7M_FPCCR, LSPEN, 30, 1)
FIELD(V7M_FPCCR, ASPEN, 31, 1)
/* These bits are banked. Others are non-banked and live in the M_REG_S bank */
#define R_V7M_FPCCR_BANKED_MASK                 \
    (R_V7M_FPCCR_LSPACT_MASK |                  \
     R_V7M_FPCCR_USER_MASK |                    \
     R_V7M_FPCCR_THREAD_MASK |                  \
     R_V7M_FPCCR_MMRDY_MASK |                   \
     R_V7M_FPCCR_SPLIMVIOL_MASK |               \
     R_V7M_FPCCR_UFRDY_MASK |                   \
     R_V7M_FPCCR_ASPEN_MASK)

/* v7M VPR bits */
FIELD(V7M_VPR, P0, 0, 16)
FIELD(V7M_VPR, MASK01, 16, 4)
FIELD(V7M_VPR, MASK23, 20, 4)

/*
 * System register ID fields.
 */
FIELD(CLIDR_EL1, CTYPE1, 0, 3)
FIELD(CLIDR_EL1, CTYPE2, 3, 3)
FIELD(CLIDR_EL1, CTYPE3, 6, 3)
FIELD(CLIDR_EL1, CTYPE4, 9, 3)
FIELD(CLIDR_EL1, CTYPE5, 12, 3)
FIELD(CLIDR_EL1, CTYPE6, 15, 3)
FIELD(CLIDR_EL1, CTYPE7, 18, 3)
FIELD(CLIDR_EL1, LOUIS, 21, 3)
FIELD(CLIDR_EL1, LOC, 24, 3)
FIELD(CLIDR_EL1, LOUU, 27, 3)
FIELD(CLIDR_EL1, ICB, 30, 3)

/* When FEAT_CCIDX is implemented */
FIELD(CCSIDR_EL1, CCIDX_LINESIZE, 0, 3)
FIELD(CCSIDR_EL1, CCIDX_ASSOCIATIVITY, 3, 21)
FIELD(CCSIDR_EL1, CCIDX_NUMSETS, 32, 24)

/* When FEAT_CCIDX is not implemented */
FIELD(CCSIDR_EL1, LINESIZE, 0, 3)
FIELD(CCSIDR_EL1, ASSOCIATIVITY, 3, 10)
FIELD(CCSIDR_EL1, NUMSETS, 13, 15)

FIELD(CTR_EL0,  IMINLINE, 0, 4)
FIELD(CTR_EL0,  L1IP, 14, 2)
FIELD(CTR_EL0,  DMINLINE, 16, 4)
FIELD(CTR_EL0,  ERG, 20, 4)
FIELD(CTR_EL0,  CWG, 24, 4)
FIELD(CTR_EL0,  IDC, 28, 1)
FIELD(CTR_EL0,  DIC, 29, 1)
FIELD(CTR_EL0,  TMINLINE, 32, 6)

FIELD(MIDR_EL1, REVISION, 0, 4)
FIELD(MIDR_EL1, PARTNUM, 4, 12)
FIELD(MIDR_EL1, ARCHITECTURE, 16, 4)
FIELD(MIDR_EL1, VARIANT, 20, 4)
FIELD(MIDR_EL1, IMPLEMENTER, 24, 8)

FIELD(ID_ISAR0, SWAP, 0, 4)
FIELD(ID_ISAR0, BITCOUNT, 4, 4)
FIELD(ID_ISAR0, BITFIELD, 8, 4)
FIELD(ID_ISAR0, CMPBRANCH, 12, 4)
FIELD(ID_ISAR0, COPROC, 16, 4)
FIELD(ID_ISAR0, DEBUG, 20, 4)
FIELD(ID_ISAR0, DIVIDE, 24, 4)

FIELD(ID_ISAR1, ENDIAN, 0, 4)
FIELD(ID_ISAR1, EXCEPT, 4, 4)
FIELD(ID_ISAR1, EXCEPT_AR, 8, 4)
FIELD(ID_ISAR1, EXTEND, 12, 4)
FIELD(ID_ISAR1, IFTHEN, 16, 4)
FIELD(ID_ISAR1, IMMEDIATE, 20, 4)
FIELD(ID_ISAR1, INTERWORK, 24, 4)
FIELD(ID_ISAR1, JAZELLE, 28, 4)

FIELD(ID_ISAR2, LOADSTORE, 0, 4)
FIELD(ID_ISAR2, MEMHINT, 4, 4)
FIELD(ID_ISAR2, MULTIACCESSINT, 8, 4)
FIELD(ID_ISAR2, MULT, 12, 4)
FIELD(ID_ISAR2, MULTS, 16, 4)
FIELD(ID_ISAR2, MULTU, 20, 4)
FIELD(ID_ISAR2, PSR_AR, 24, 4)
FIELD(ID_ISAR2, REVERSAL, 28, 4)

FIELD(ID_ISAR3, SATURATE, 0, 4)
FIELD(ID_ISAR3, SIMD, 4, 4)
FIELD(ID_ISAR3, SVC, 8, 4)
FIELD(ID_ISAR3, SYNCHPRIM, 12, 4)
FIELD(ID_ISAR3, TABBRANCH, 16, 4)
FIELD(ID_ISAR3, T32COPY, 20, 4)
FIELD(ID_ISAR3, TRUENOP, 24, 4)
FIELD(ID_ISAR3, T32EE, 28, 4)

FIELD(ID_ISAR4, UNPRIV, 0, 4)
FIELD(ID_ISAR4, WITHSHIFTS, 4, 4)
FIELD(ID_ISAR4, WRITEBACK, 8, 4)
FIELD(ID_ISAR4, SMC, 12, 4)
FIELD(ID_ISAR4, BARRIER, 16, 4)
FIELD(ID_ISAR4, SYNCHPRIM_FRAC, 20, 4)
FIELD(ID_ISAR4, PSR_M, 24, 4)
FIELD(ID_ISAR4, SWP_FRAC, 28, 4)

FIELD(ID_ISAR5, SEVL, 0, 4)
FIELD(ID_ISAR5, AES, 4, 4)
FIELD(ID_ISAR5, SHA1, 8, 4)
FIELD(ID_ISAR5, SHA2, 12, 4)
FIELD(ID_ISAR5, CRC32, 16, 4)
FIELD(ID_ISAR5, RDM, 24, 4)
FIELD(ID_ISAR5, VCMA, 28, 4)

FIELD(ID_ISAR6, JSCVT, 0, 4)
FIELD(ID_ISAR6, DP, 4, 4)
FIELD(ID_ISAR6, FHM, 8, 4)
FIELD(ID_ISAR6, SB, 12, 4)
FIELD(ID_ISAR6, SPECRES, 16, 4)
FIELD(ID_ISAR6, BF16, 20, 4)
FIELD(ID_ISAR6, I8MM, 24, 4)

FIELD(ID_MMFR0, VMSA, 0, 4)
FIELD(ID_MMFR0, PMSA, 4, 4)
FIELD(ID_MMFR0, OUTERSHR, 8, 4)
FIELD(ID_MMFR0, SHARELVL, 12, 4)
FIELD(ID_MMFR0, TCM, 16, 4)
FIELD(ID_MMFR0, AUXREG, 20, 4)
FIELD(ID_MMFR0, FCSE, 24, 4)
FIELD(ID_MMFR0, INNERSHR, 28, 4)

FIELD(ID_MMFR1, L1HVDVA, 0, 4)
FIELD(ID_MMFR1, L1UNIVA, 4, 4)
FIELD(ID_MMFR1, L1HVDSW, 8, 4)
FIELD(ID_MMFR1, L1UNISW, 12, 4)
FIELD(ID_MMFR1, L1HVD, 16, 4)
FIELD(ID_MMFR1, L1UNI, 20, 4)
FIELD(ID_MMFR1, L1TSTCLN, 24, 4)
FIELD(ID_MMFR1, BPRED, 28, 4)

FIELD(ID_MMFR2, L1HVDFG, 0, 4)
FIELD(ID_MMFR2, L1HVDBG, 4, 4)
FIELD(ID_MMFR2, L1HVDRNG, 8, 4)
FIELD(ID_MMFR2, HVDTLB, 12, 4)
FIELD(ID_MMFR2, UNITLB, 16, 4)
FIELD(ID_MMFR2, MEMBARR, 20, 4)
FIELD(ID_MMFR2, WFISTALL, 24, 4)
FIELD(ID_MMFR2, HWACCFLG, 28, 4)

FIELD(ID_MMFR3, CMAINTVA, 0, 4)
FIELD(ID_MMFR3, CMAINTSW, 4, 4)
FIELD(ID_MMFR3, BPMAINT, 8, 4)
FIELD(ID_MMFR3, MAINTBCST, 12, 4)
FIELD(ID_MMFR3, PAN, 16, 4)
FIELD(ID_MMFR3, COHWALK, 20, 4)
FIELD(ID_MMFR3, CMEMSZ, 24, 4)
FIELD(ID_MMFR3, SUPERSEC, 28, 4)

FIELD(ID_MMFR4, SPECSEI, 0, 4)
FIELD(ID_MMFR4, AC2, 4, 4)
FIELD(ID_MMFR4, XNX, 8, 4)
FIELD(ID_MMFR4, CNP, 12, 4)
FIELD(ID_MMFR4, HPDS, 16, 4)
FIELD(ID_MMFR4, LSM, 20, 4)
FIELD(ID_MMFR4, CCIDX, 24, 4)
FIELD(ID_MMFR4, EVT, 28, 4)

FIELD(ID_MMFR5, ETS, 0, 4)

FIELD(ID_PFR0, STATE0, 0, 4)
FIELD(ID_PFR0, STATE1, 4, 4)
FIELD(ID_PFR0, STATE2, 8, 4)
FIELD(ID_PFR0, STATE3, 12, 4)
FIELD(ID_PFR0, CSV2, 16, 4)
FIELD(ID_PFR0, AMU, 20, 4)
FIELD(ID_PFR0, DIT, 24, 4)
FIELD(ID_PFR0, RAS, 28, 4)

FIELD(ID_PFR1, PROGMOD, 0, 4)
FIELD(ID_PFR1, SECURITY, 4, 4)
FIELD(ID_PFR1, MPROGMOD, 8, 4)
FIELD(ID_PFR1, VIRTUALIZATION, 12, 4)
FIELD(ID_PFR1, GENTIMER, 16, 4)
FIELD(ID_PFR1, SEC_FRAC, 20, 4)
FIELD(ID_PFR1, VIRT_FRAC, 24, 4)
FIELD(ID_PFR1, GIC, 28, 4)

FIELD(ID_PFR2, CSV3, 0, 4)
FIELD(ID_PFR2, SSBS, 4, 4)
FIELD(ID_PFR2, RAS_FRAC, 8, 4)

FIELD(ID_AA64ISAR0, AES, 4, 4)
FIELD(ID_AA64ISAR0, SHA1, 8, 4)
FIELD(ID_AA64ISAR0, SHA2, 12, 4)
FIELD(ID_AA64ISAR0, CRC32, 16, 4)
FIELD(ID_AA64ISAR0, ATOMIC, 20, 4)
FIELD(ID_AA64ISAR0, RDM, 28, 4)
FIELD(ID_AA64ISAR0, SHA3, 32, 4)
FIELD(ID_AA64ISAR0, SM3, 36, 4)
FIELD(ID_AA64ISAR0, SM4, 40, 4)
FIELD(ID_AA64ISAR0, DP, 44, 4)
FIELD(ID_AA64ISAR0, FHM, 48, 4)
FIELD(ID_AA64ISAR0, TS, 52, 4)
FIELD(ID_AA64ISAR0, TLB, 56, 4)
FIELD(ID_AA64ISAR0, RNDR, 60, 4)

FIELD(ID_AA64ISAR1, DPB, 0, 4)
FIELD(ID_AA64ISAR1, APA, 4, 4)
FIELD(ID_AA64ISAR1, API, 8, 4)
FIELD(ID_AA64ISAR1, JSCVT, 12, 4)
FIELD(ID_AA64ISAR1, FCMA, 16, 4)
FIELD(ID_AA64ISAR1, LRCPC, 20, 4)
FIELD(ID_AA64ISAR1, GPA, 24, 4)
FIELD(ID_AA64ISAR1, GPI, 28, 4)
FIELD(ID_AA64ISAR1, FRINTTS, 32, 4)
FIELD(ID_AA64ISAR1, SB, 36, 4)
FIELD(ID_AA64ISAR1, SPECRES, 40, 4)
FIELD(ID_AA64ISAR1, BF16, 44, 4)
FIELD(ID_AA64ISAR1, DGH, 48, 4)
FIELD(ID_AA64ISAR1, I8MM, 52, 4)

FIELD(ID_AA64PFR0, EL0, 0, 4)
FIELD(ID_AA64PFR0, EL1, 4, 4)
FIELD(ID_AA64PFR0, EL2, 8, 4)
FIELD(ID_AA64PFR0, EL3, 12, 4)
FIELD(ID_AA64PFR0, FP, 16, 4)
FIELD(ID_AA64PFR0, ADVSIMD, 20, 4)
FIELD(ID_AA64PFR0, GIC, 24, 4)
FIELD(ID_AA64PFR0, RAS, 28, 4)
FIELD(ID_AA64PFR0, SVE, 32, 4)
FIELD(ID_AA64PFR0, SEL2, 36, 4)
FIELD(ID_AA64PFR0, MPAM, 40, 4)
FIELD(ID_AA64PFR0, AMU, 44, 4)
FIELD(ID_AA64PFR0, DIT, 48, 4)
FIELD(ID_AA64PFR0, CSV2, 56, 4)
FIELD(ID_AA64PFR0, CSV3, 60, 4)

FIELD(ID_AA64PFR1, BT, 0, 4)
FIELD(ID_AA64PFR1, SSBS, 4, 4)
FIELD(ID_AA64PFR1, MTE, 8, 4)
FIELD(ID_AA64PFR1, RAS_FRAC, 12, 4)
FIELD(ID_AA64PFR1, MPAM_FRAC, 16, 4)

FIELD(ID_AA64MMFR0, PARANGE, 0, 4)
FIELD(ID_AA64MMFR0, ASIDBITS, 4, 4)
FIELD(ID_AA64MMFR0, BIGEND, 8, 4)
FIELD(ID_AA64MMFR0, SNSMEM, 12, 4)
FIELD(ID_AA64MMFR0, BIGENDEL0, 16, 4)
FIELD(ID_AA64MMFR0, TGRAN16, 20, 4)
FIELD(ID_AA64MMFR0, TGRAN64, 24, 4)
FIELD(ID_AA64MMFR0, TGRAN4, 28, 4)
FIELD(ID_AA64MMFR0, TGRAN16_2, 32, 4)
FIELD(ID_AA64MMFR0, TGRAN64_2, 36, 4)
FIELD(ID_AA64MMFR0, TGRAN4_2, 40, 4)
FIELD(ID_AA64MMFR0, EXS, 44, 4)
FIELD(ID_AA64MMFR0, FGT, 56, 4)
FIELD(ID_AA64MMFR0, ECV, 60, 4)

FIELD(ID_AA64MMFR1, HAFDBS, 0, 4)
FIELD(ID_AA64MMFR1, VMIDBITS, 4, 4)
FIELD(ID_AA64MMFR1, VH, 8, 4)
FIELD(ID_AA64MMFR1, HPDS, 12, 4)
FIELD(ID_AA64MMFR1, LO, 16, 4)
FIELD(ID_AA64MMFR1, PAN, 20, 4)
FIELD(ID_AA64MMFR1, SPECSEI, 24, 4)
FIELD(ID_AA64MMFR1, XNX, 28, 4)
FIELD(ID_AA64MMFR1, TWED, 32, 4)
FIELD(ID_AA64MMFR1, ETS, 36, 4)

FIELD(ID_AA64MMFR2, CNP, 0, 4)
FIELD(ID_AA64MMFR2, UAO, 4, 4)
FIELD(ID_AA64MMFR2, LSM, 8, 4)
FIELD(ID_AA64MMFR2, IESB, 12, 4)
FIELD(ID_AA64MMFR2, VARANGE, 16, 4)
FIELD(ID_AA64MMFR2, CCIDX, 20, 4)
FIELD(ID_AA64MMFR2, NV, 24, 4)
FIELD(ID_AA64MMFR2, ST, 28, 4)
FIELD(ID_AA64MMFR2, AT, 32, 4)
FIELD(ID_AA64MMFR2, IDS, 36, 4)
FIELD(ID_AA64MMFR2, FWB, 40, 4)
FIELD(ID_AA64MMFR2, TTL, 48, 4)
FIELD(ID_AA64MMFR2, BBM, 52, 4)
FIELD(ID_AA64MMFR2, EVT, 56, 4)
FIELD(ID_AA64MMFR2, E0PD, 60, 4)

FIELD(ID_AA64DFR0, DEBUGVER, 0, 4)
FIELD(ID_AA64DFR0, TRACEVER, 4, 4)
FIELD(ID_AA64DFR0, PMUVER, 8, 4)
FIELD(ID_AA64DFR0, BRPS, 12, 4)
FIELD(ID_AA64DFR0, WRPS, 20, 4)
FIELD(ID_AA64DFR0, CTX_CMPS, 28, 4)
FIELD(ID_AA64DFR0, PMSVER, 32, 4)
FIELD(ID_AA64DFR0, DOUBLELOCK, 36, 4)
FIELD(ID_AA64DFR0, TRACEFILT, 40, 4)
FIELD(ID_AA64DFR0, MTPMU, 48, 4)

FIELD(ID_AA64ZFR0, SVEVER, 0, 4)
FIELD(ID_AA64ZFR0, AES, 4, 4)
FIELD(ID_AA64ZFR0, BITPERM, 16, 4)
FIELD(ID_AA64ZFR0, BFLOAT16, 20, 4)
FIELD(ID_AA64ZFR0, SHA3, 32, 4)
FIELD(ID_AA64ZFR0, SM4, 40, 4)
FIELD(ID_AA64ZFR0, I8MM, 44, 4)
FIELD(ID_AA64ZFR0, F32MM, 52, 4)
FIELD(ID_AA64ZFR0, F64MM, 56, 4)

FIELD(ID_DFR0, COPDBG, 0, 4)
FIELD(ID_DFR0, COPSDBG, 4, 4)
FIELD(ID_DFR0, MMAPDBG, 8, 4)
FIELD(ID_DFR0, COPTRC, 12, 4)
FIELD(ID_DFR0, MMAPTRC, 16, 4)
FIELD(ID_DFR0, MPROFDBG, 20, 4)
FIELD(ID_DFR0, PERFMON, 24, 4)
FIELD(ID_DFR0, TRACEFILT, 28, 4)

FIELD(ID_DFR1, MTPMU, 0, 4)

FIELD(DBGDIDR, SE_IMP, 12, 1)
FIELD(DBGDIDR, NSUHD_IMP, 14, 1)
FIELD(DBGDIDR, VERSION, 16, 4)
FIELD(DBGDIDR, CTX_CMPS, 20, 4)
FIELD(DBGDIDR, BRPS, 24, 4)
FIELD(DBGDIDR, WRPS, 28, 4)

FIELD(MVFR0, SIMDREG, 0, 4)
FIELD(MVFR0, FPSP, 4, 4)
FIELD(MVFR0, FPDP, 8, 4)
FIELD(MVFR0, FPTRAP, 12, 4)
FIELD(MVFR0, FPDIVIDE, 16, 4)
FIELD(MVFR0, FPSQRT, 20, 4)
FIELD(MVFR0, FPSHVEC, 24, 4)
FIELD(MVFR0, FPROUND, 28, 4)

FIELD(MVFR1, FPFTZ, 0, 4)
FIELD(MVFR1, FPDNAN, 4, 4)
FIELD(MVFR1, SIMDLS, 8, 4) /* A-profile only */
FIELD(MVFR1, SIMDINT, 12, 4) /* A-profile only */
FIELD(MVFR1, SIMDSP, 16, 4) /* A-profile only */
FIELD(MVFR1, SIMDHP, 20, 4) /* A-profile only */
FIELD(MVFR1, MVE, 8, 4) /* M-profile only */
FIELD(MVFR1, FP16, 20, 4) /* M-profile only */
FIELD(MVFR1, FPHP, 24, 4)
FIELD(MVFR1, SIMDFMAC, 28, 4)

FIELD(MVFR2, SIMDMISC, 0, 4)
FIELD(MVFR2, FPMISC, 4, 4)

QEMU_BUILD_BUG_ON(ARRAY_SIZE(((ARMCPU *)0)->ccsidr) <= R_V7M_CSSELR_INDEX_MASK);

/* If adding a feature bit which corresponds to a Linux ELF
 * HWCAP bit, remember to update the feature-bit-to-hwcap
 * mapping in linux-user/elfload.c:get_elf_hwcap().
 */
enum arm_features {
    ARM_FEATURE_AUXCR,  /* ARM1026 Auxiliary control register.  */
    ARM_FEATURE_XSCALE, /* Intel XScale extensions.  */
    ARM_FEATURE_IWMMXT, /* Intel iwMMXt extension.  */
    ARM_FEATURE_V6,
    ARM_FEATURE_V6K,
    ARM_FEATURE_V7,
    ARM_FEATURE_THUMB2,
    ARM_FEATURE_PMSA,   /* no MMU; may have Memory Protection Unit */
    ARM_FEATURE_NEON,
    ARM_FEATURE_M, /* Microcontroller profile.  */
    ARM_FEATURE_OMAPCP, /* OMAP specific CP15 ops handling.  */
    ARM_FEATURE_THUMB2EE,
    ARM_FEATURE_V7MP,    /* v7 Multiprocessing Extensions */
    ARM_FEATURE_V7VE, /* v7 Virtualization Extensions (non-EL2 parts) */
    ARM_FEATURE_V4T,
    ARM_FEATURE_V5,
    ARM_FEATURE_STRONGARM,
    ARM_FEATURE_VAPA, /* cp15 VA to PA lookups */
    ARM_FEATURE_GENERIC_TIMER,
    ARM_FEATURE_MVFR, /* Media and VFP Feature Registers 0 and 1 */
    ARM_FEATURE_DUMMY_C15_REGS, /* RAZ/WI all of cp15 crn=15 */
    ARM_FEATURE_CACHE_TEST_CLEAN, /* 926/1026 style test-and-clean ops */
    ARM_FEATURE_CACHE_DIRTY_REG, /* 1136/1176 cache dirty status register */
    ARM_FEATURE_CACHE_BLOCK_OPS, /* v6 optional cache block operations */
    ARM_FEATURE_MPIDR, /* has cp15 MPIDR */
    ARM_FEATURE_LPAE, /* has Large Physical Address Extension */
    ARM_FEATURE_V8,
    ARM_FEATURE_AARCH64, /* supports 64 bit mode */
    ARM_FEATURE_CBAR, /* has cp15 CBAR */
    ARM_FEATURE_CBAR_RO, /* has cp15 CBAR and it is read-only */
    ARM_FEATURE_EL2, /* has EL2 Virtualization support */
    ARM_FEATURE_EL3, /* has EL3 Secure monitor support */
    ARM_FEATURE_THUMB_DSP, /* DSP insns supported in the Thumb encodings */
    ARM_FEATURE_PMU, /* has PMU support */
    ARM_FEATURE_VBAR, /* has cp15 VBAR */
    ARM_FEATURE_M_SECURITY, /* M profile Security Extension */
    ARM_FEATURE_M_MAIN, /* M profile Main Extension */
    ARM_FEATURE_V8_1M, /* M profile extras only in v8.1M and later */
};

static inline int arm_feature(CPUARMState *env, int feature)
{
    return (env->features & (1ULL << feature)) != 0;
}

void arm_cpu_finalize_features(ARMCPU *cpu, Error **errp);

#if !defined(CONFIG_USER_ONLY)
/* Return true if exception levels below EL3 are in secure state,
 * or would be following an exception return to that level.
 * Unlike arm_is_secure() (which is always a question about the
 * _current_ state of the CPU) this doesn't care about the current
 * EL or mode.
 */
static inline bool arm_is_secure_below_el3(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        return !(env->cp15.scr_el3 & SCR_NS);
    } else {
        /* If EL3 is not supported then the secure state is implementation
         * defined, in which case QEMU defaults to non-secure.
         */
        return false;
    }
}

/* Return true if the CPU is AArch64 EL3 or AArch32 Mon */
static inline bool arm_is_el3_or_mon(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (is_a64(env) && extract32(env->pstate, 2, 2) == 3) {
            /* CPU currently in AArch64 state and EL3 */
            return true;
        } else if (!is_a64(env) &&
                (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
            /* CPU currently in AArch32 state and monitor mode */
            return true;
        }
    }
    return false;
}

/* Return true if the processor is in secure state */
static inline bool arm_is_secure(CPUARMState *env)
{
    if (arm_is_el3_or_mon(env)) {
        return true;
    }
    return arm_is_secure_below_el3(env);
}

/*
 * Return true if the current security state has AArch64 EL2 or AArch32 Hyp.
 * This corresponds to the pseudocode EL2Enabled()
 */
static inline bool arm_is_el2_enabled(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_EL2)) {
        if (arm_is_secure_below_el3(env)) {
            return (env->cp15.scr_el3 & SCR_EEL2) != 0;
        }
        return true;
    }
    return false;
}

#else
static inline bool arm_is_secure_below_el3(CPUARMState *env)
{
    return false;
}

static inline bool arm_is_secure(CPUARMState *env)
{
    return false;
}

static inline bool arm_is_el2_enabled(CPUARMState *env)
{
    return false;
}
#endif

/**
 * arm_hcr_el2_eff(): Return the effective value of HCR_EL2.
 * E.g. when in secure state, fields in HCR_EL2 are suppressed,
 * "for all purposes other than a direct read or write access of HCR_EL2."
 * Not included here is HCR_RW.
 */
uint64_t arm_hcr_el2_eff(CPUARMState *env);

/* Return true if the specified exception level is running in AArch64 state. */
static inline bool arm_el_is_aa64(CPUARMState *env, int el)
{
    /* This isn't valid for EL0 (if we're in EL0, is_a64() is what you want,
     * and if we're not in EL0 then the state of EL0 isn't well defined.)
     */
    assert(el >= 1 && el <= 3);
    bool aa64 = arm_feature(env, ARM_FEATURE_AARCH64);

    /* The highest exception level is always at the maximum supported
     * register width, and then lower levels have a register width controlled
     * by bits in the SCR or HCR registers.
     */
    if (el == 3) {
        return aa64;
    }

    if (arm_feature(env, ARM_FEATURE_EL3) &&
        ((env->cp15.scr_el3 & SCR_NS) || !(env->cp15.scr_el3 & SCR_EEL2))) {
        aa64 = aa64 && (env->cp15.scr_el3 & SCR_RW);
    }

    if (el == 2) {
        return aa64;
    }

    if (arm_is_el2_enabled(env)) {
        aa64 = aa64 && (env->cp15.hcr_el2 & HCR_RW);
    }

    return aa64;
}

/* Function for determing whether guest cp register reads and writes should
 * access the secure or non-secure bank of a cp register.  When EL3 is
 * operating in AArch32 state, the NS-bit determines whether the secure
 * instance of a cp register should be used. When EL3 is AArch64 (or if
 * it doesn't exist at all) then there is no register banking, and all
 * accesses are to the non-secure version.
 */
static inline bool access_secure_reg(CPUARMState *env)
{
    bool ret = (arm_feature(env, ARM_FEATURE_EL3) &&
                !arm_el_is_aa64(env, 3) &&
                !(env->cp15.scr_el3 & SCR_NS));

    return ret;
}

/* Macros for accessing a specified CP register bank */
#define A32_BANKED_REG_GET(_env, _regname, _secure)    \
    ((_secure) ? (_env)->cp15._regname##_s : (_env)->cp15._regname##_ns)

#define A32_BANKED_REG_SET(_env, _regname, _secure, _val)   \
    do {                                                \
        if (_secure) {                                   \
            (_env)->cp15._regname##_s = (_val);            \
        } else {                                        \
            (_env)->cp15._regname##_ns = (_val);           \
        }                                               \
    } while (0)

/* Macros for automatically accessing a specific CP register bank depending on
 * the current secure state of the system.  These macros are not intended for
 * supporting instruction translation reads/writes as these are dependent
 * solely on the SCR.NS bit and not the mode.
 */
#define A32_BANKED_CURRENT_REG_GET(_env, _regname)        \
    A32_BANKED_REG_GET((_env), _regname,                \
                       (arm_is_secure(_env) && !arm_el_is_aa64((_env), 3)))

#define A32_BANKED_CURRENT_REG_SET(_env, _regname, _val)                       \
    A32_BANKED_REG_SET((_env), _regname,                                    \
                       (arm_is_secure(_env) && !arm_el_is_aa64((_env), 3)), \
                       (_val))

void arm_cpu_list(void);
uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure);

/* Interface between CPU and Interrupt controller.  */
#ifndef CONFIG_USER_ONLY
bool armv7m_nvic_can_take_pending_exception(void *opaque);
#else
static inline bool armv7m_nvic_can_take_pending_exception(void *opaque)
{
    return true;
}
#endif
/**
 * armv7m_nvic_set_pending: mark the specified exception as pending
 * @opaque: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Marks the specified exception as pending. Note that we will assert()
 * if @secure is true and @irq does not specify one of the fixed set
 * of architecturally banked exceptions.
 */
void armv7m_nvic_set_pending(void *opaque, int irq, bool secure);
/**
 * armv7m_nvic_set_pending_derived: mark this derived exception as pending
 * @opaque: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Similar to armv7m_nvic_set_pending(), but specifically for derived
 * exceptions (exceptions generated in the course of trying to take
 * a different exception).
 */
void armv7m_nvic_set_pending_derived(void *opaque, int irq, bool secure);
/**
 * armv7m_nvic_set_pending_lazyfp: mark this lazy FP exception as pending
 * @opaque: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Similar to armv7m_nvic_set_pending(), but specifically for exceptions
 * generated in the course of lazy stacking of FP registers.
 */
void armv7m_nvic_set_pending_lazyfp(void *opaque, int irq, bool secure);
/**
 * armv7m_nvic_get_pending_irq_info: return highest priority pending
 *    exception, and whether it targets Secure state
 * @opaque: the NVIC
 * @pirq: set to pending exception number
 * @ptargets_secure: set to whether pending exception targets Secure
 *
 * This function writes the number of the highest priority pending
 * exception (the one which would be made active by
 * armv7m_nvic_acknowledge_irq()) to @pirq, and sets @ptargets_secure
 * to true if the current highest priority pending exception should
 * be taken to Secure state, false for NS.
 */
void armv7m_nvic_get_pending_irq_info(void *opaque, int *pirq,
                                      bool *ptargets_secure);
/**
 * armv7m_nvic_acknowledge_irq: make highest priority pending exception active
 * @opaque: the NVIC
 *
 * Move the current highest priority pending exception from the pending
 * state to the active state, and update v7m.exception to indicate that
 * it is the exception currently being handled.
 */
void armv7m_nvic_acknowledge_irq(void *opaque);
/**
 * armv7m_nvic_complete_irq: complete specified interrupt or exception
 * @opaque: the NVIC
 * @irq: the exception number to complete
 * @secure: true if this exception was secure
 *
 * Returns: -1 if the irq was not active
 *           1 if completing this irq brought us back to base (no active irqs)
 *           0 if there is still an irq active after this one was completed
 * (Ignoring -1, this is the same as the RETTOBASE value before completion.)
 */
int armv7m_nvic_complete_irq(void *opaque, int irq, bool secure);
/**
 * armv7m_nvic_get_ready_status(void *opaque, int irq, bool secure)
 * @opaque: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Return whether an exception is "ready", i.e. whether the exception is
 * enabled and is configured at a priority which would allow it to
 * interrupt the current execution priority. This controls whether the
 * RDY bit for it in the FPCCR is set.
 */
bool armv7m_nvic_get_ready_status(void *opaque, int irq, bool secure);
/**
 * armv7m_nvic_raw_execution_priority: return the raw execution priority
 * @opaque: the NVIC
 *
 * Returns: the raw execution priority as defined by the v8M architecture.
 * This is the execution priority minus the effects of AIRCR.PRIS,
 * and minus any PRIMASK/FAULTMASK/BASEPRI priority boosting.
 * (v8M ARM ARM I_PKLD.)
 */
int armv7m_nvic_raw_execution_priority(void *opaque);
/**
 * armv7m_nvic_neg_prio_requested: return true if the requested execution
 * priority is negative for the specified security state.
 * @opaque: the NVIC
 * @secure: the security state to test
 * This corresponds to the pseudocode IsReqExecPriNeg().
 */
#ifndef CONFIG_USER_ONLY
bool armv7m_nvic_neg_prio_requested(void *opaque, bool secure);
#else
static inline bool armv7m_nvic_neg_prio_requested(void *opaque, bool secure)
{
    return false;
}
#endif

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
 *  non-secure/secure bank (AArch32 only)
 * We allow 4 bits for opc1 because MRRC/MCRR have a 4 bit field.
 * (In this case crn and opc2 should be zero.)
 * For AArch64, there is no 32/64 bit size distinction;
 * instead all registers have a 2 bit op0, 3 bit op1 and op2,
 * and 4 bit CRn and CRm. The encoding patterns are chosen
 * to be easy to convert to and from the KVM encodings, and also
 * so that the hashtable can contain both AArch32 and AArch64
 * registers (to allow for interprocessing where we might run
 * 32 bit code on a 64 bit core).
 */
/* This bit is private to our hashtable cpreg; in KVM register
 * IDs the AArch64/32 distinction is the KVM_REG_ARM/ARM64
 * in the upper bits of the 64 bit ID.
 */
#define CP_REG_AA64_SHIFT 28
#define CP_REG_AA64_MASK (1 << CP_REG_AA64_SHIFT)

/* To enable banking of coprocessor registers depending on ns-bit we
 * add a bit to distinguish between secure and non-secure cpregs in the
 * hashtable.
 */
#define CP_REG_NS_SHIFT 29
#define CP_REG_NS_MASK (1 << CP_REG_NS_SHIFT)

#define ENCODE_CP_REG(cp, is64, ns, crn, crm, opc1, opc2)   \
    ((ns) << CP_REG_NS_SHIFT | ((cp) << 16) | ((is64) << 15) |   \
     ((crn) << 11) | ((crm) << 7) | ((opc1) << 3) | (opc2))

#define ENCODE_AA64_CP_REG(cp, crn, crm, op0, op1, op2) \
    (CP_REG_AA64_MASK |                                 \
     ((cp) << CP_REG_ARM_COPROC_SHIFT) |                \
     ((op0) << CP_REG_ARM64_SYSREG_OP0_SHIFT) |         \
     ((op1) << CP_REG_ARM64_SYSREG_OP1_SHIFT) |         \
     ((crn) << CP_REG_ARM64_SYSREG_CRN_SHIFT) |         \
     ((crm) << CP_REG_ARM64_SYSREG_CRM_SHIFT) |         \
     ((op2) << CP_REG_ARM64_SYSREG_OP2_SHIFT))

/* Convert a full 64 bit KVM register ID to the truncated 32 bit
 * version used as a key for the coprocessor register hashtable
 */
static inline uint32_t kvm_to_cpreg_id(uint64_t kvmid)
{
    uint32_t cpregid = kvmid;
    if ((kvmid & CP_REG_ARCH_MASK) == CP_REG_ARM64) {
        cpregid |= CP_REG_AA64_MASK;
    } else {
        if ((kvmid & CP_REG_SIZE_MASK) == CP_REG_SIZE_U64) {
            cpregid |= (1 << 15);
        }

        /* KVM is always non-secure so add the NS flag on AArch32 register
         * entries.
         */
         cpregid |= 1 << CP_REG_NS_SHIFT;
    }
    return cpregid;
}

/* Convert a truncated 32 bit hashtable key into the full
 * 64 bit KVM register ID.
 */
static inline uint64_t cpreg_to_kvm_id(uint32_t cpregid)
{
    uint64_t kvmid;

    if (cpregid & CP_REG_AA64_MASK) {
        kvmid = cpregid & ~CP_REG_AA64_MASK;
        kvmid |= CP_REG_SIZE_U64 | CP_REG_ARM64;
    } else {
        kvmid = cpregid & ~(1 << 15);
        if (cpregid & (1 << 15)) {
            kvmid |= CP_REG_SIZE_U64 | CP_REG_ARM;
        } else {
            kvmid |= CP_REG_SIZE_U32 | CP_REG_ARM;
        }
    }
    return kvmid;
}

/* ARMCPRegInfo type field bits. If the SPECIAL bit is set this is a
 * special-behaviour cp reg and bits [11..8] indicate what behaviour
 * it has. Otherwise it is a simple cp reg, where CONST indicates that
 * TCG can assume the value to be constant (ie load at translate time)
 * and 64BIT indicates a 64 bit wide coprocessor register. SUPPRESS_TB_END
 * indicates that the TB should not be ended after a write to this register
 * (the default is that the TB ends after cp writes). OVERRIDE permits
 * a register definition to override a previous definition for the
 * same (cp, is64, crn, crm, opc1, opc2) tuple: either the new or the
 * old must have the OVERRIDE bit set.
 * ALIAS indicates that this register is an alias view of some underlying
 * state which is also visible via another register, and that the other
 * register is handling migration and reset; registers marked ALIAS will not be
 * migrated but may have their state set by syncing of register state from KVM.
 * NO_RAW indicates that this register has no underlying state and does not
 * support raw access for state saving/loading; it will not be used for either
 * migration or KVM state synchronization. (Typically this is for "registers"
 * which are actually used as instructions for cache maintenance and so on.)
 * IO indicates that this register does I/O and therefore its accesses
 * need to be marked with gen_io_start() and also end the TB. In particular,
 * registers which implement clocks or timers require this.
 * RAISES_EXC is for when the read or write hook might raise an exception;
 * the generated code will synchronize the CPU state before calling the hook
 * so that it is safe for the hook to call raise_exception().
 * NEWEL is for writes to registers that might change the exception
 * level - typically on older ARM chips. For those cases we need to
 * re-read the new el when recomputing the translation flags.
 */
#define ARM_CP_SPECIAL           0x0001
#define ARM_CP_CONST             0x0002
#define ARM_CP_64BIT             0x0004
#define ARM_CP_SUPPRESS_TB_END   0x0008
#define ARM_CP_OVERRIDE          0x0010
#define ARM_CP_ALIAS             0x0020
#define ARM_CP_IO                0x0040
#define ARM_CP_NO_RAW            0x0080
#define ARM_CP_NOP               (ARM_CP_SPECIAL | 0x0100)
#define ARM_CP_WFI               (ARM_CP_SPECIAL | 0x0200)
#define ARM_CP_NZCV              (ARM_CP_SPECIAL | 0x0300)
#define ARM_CP_CURRENTEL         (ARM_CP_SPECIAL | 0x0400)
#define ARM_CP_DC_ZVA            (ARM_CP_SPECIAL | 0x0500)
#define ARM_CP_DC_GVA            (ARM_CP_SPECIAL | 0x0600)
#define ARM_CP_DC_GZVA           (ARM_CP_SPECIAL | 0x0700)
#define ARM_LAST_SPECIAL         ARM_CP_DC_GZVA
#define ARM_CP_FPU               0x1000
#define ARM_CP_SVE               0x2000
#define ARM_CP_NO_GDB            0x4000
#define ARM_CP_RAISES_EXC        0x8000
#define ARM_CP_NEWEL             0x10000
/* Used only as a terminator for ARMCPRegInfo lists */
#define ARM_CP_SENTINEL          0xfffff
/* Mask of only the flag bits in a type field */
#define ARM_CP_FLAG_MASK         0x1f0ff

/* Valid values for ARMCPRegInfo state field, indicating which of
 * the AArch32 and AArch64 execution states this register is visible in.
 * If the reginfo doesn't explicitly specify then it is AArch32 only.
 * If the reginfo is declared to be visible in both states then a second
 * reginfo is synthesised for the AArch32 view of the AArch64 register,
 * such that the AArch32 view is the lower 32 bits of the AArch64 one.
 * Note that we rely on the values of these enums as we iterate through
 * the various states in some places.
 */
enum {
    ARM_CP_STATE_AA32 = 0,
    ARM_CP_STATE_AA64 = 1,
    ARM_CP_STATE_BOTH = 2,
};

/* ARM CP register secure state flags.  These flags identify security state
 * attributes for a given CP register entry.
 * The existence of both or neither secure and non-secure flags indicates that
 * the register has both a secure and non-secure hash entry.  A single one of
 * these flags causes the register to only be hashed for the specified
 * security state.
 * Although definitions may have any combination of the S/NS bits, each
 * registered entry will only have one to identify whether the entry is secure
 * or non-secure.
 */
enum {
    ARM_CP_SECSTATE_S =   (1 << 0), /* bit[0]: Secure state register */
    ARM_CP_SECSTATE_NS =  (1 << 1), /* bit[1]: Non-secure state register */
};

/* Return true if cptype is a valid type field. This is used to try to
 * catch errors where the sentinel has been accidentally left off the end
 * of a list of registers.
 */
static inline bool cptype_valid(int cptype)
{
    return ((cptype & ~ARM_CP_FLAG_MASK) == 0)
        || ((cptype & ARM_CP_SPECIAL) &&
            ((cptype & ~ARM_CP_FLAG_MASK) <= ARM_LAST_SPECIAL));
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
 * In AArch64 things are somewhat simpler as the PLx bits line up exactly
 * with the ELx exception levels.
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

/*
 * For user-mode some registers are accessible to EL0 via a kernel
 * trap-and-emulate ABI. In this case we define the read permissions
 * as actually being PL0_R. However some bits of any given register
 * may still be masked.
 */
#ifdef CONFIG_USER_ONLY
#define PL0U_R PL0_R
#else
#define PL0U_R PL1_R
#endif

#define PL3_RW (PL3_R | PL3_W)
#define PL2_RW (PL2_R | PL2_W)
#define PL1_RW (PL1_R | PL1_W)
#define PL0_RW (PL0_R | PL0_W)

/* Return the highest implemented Exception Level */
static inline int arm_highest_el(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        return 3;
    }
    if (arm_feature(env, ARM_FEATURE_EL2)) {
        return 2;
    }
    return 1;
}

/* Return true if a v7M CPU is in Handler mode */
static inline bool arm_v7m_is_handler_mode(CPUARMState *env)
{
    return env->v7m.exception != 0;
}

/* Return the current Exception Level (as per ARMv8; note that this differs
 * from the ARMv7 Privilege Level).
 */
static inline int arm_current_el(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        return arm_v7m_is_handler_mode(env) ||
            !(env->v7m.control[env->v7m.secure] & 1);
    }

    if (is_a64(env)) {
        return extract32(env->pstate, 2, 2);
    }

    switch (env->uncached_cpsr & 0x1f) {
    case ARM_CPU_MODE_USR:
        return 0;
    case ARM_CPU_MODE_HYP:
        return 2;
    case ARM_CPU_MODE_MON:
        return 3;
    default:
        if (arm_is_secure(env) && !arm_el_is_aa64(env, 3)) {
            /* If EL3 is 32-bit then all secure privileged modes run in
             * EL3
             */
            return 3;
        }

        return 1;
    }
}

typedef struct ARMCPRegInfo ARMCPRegInfo;

typedef enum CPAccessResult {
    /* Access is permitted */
    CP_ACCESS_OK = 0,
    /* Access fails due to a configurable trap or enable which would
     * result in a categorized exception syndrome giving information about
     * the failing instruction (ie syndrome category 0x3, 0x4, 0x5, 0x6,
     * 0xc or 0x18). The exception is taken to the usual target EL (EL1 or
     * PL1 if in EL0, otherwise to the current EL).
     */
    CP_ACCESS_TRAP = 1,
    /* Access fails and results in an exception syndrome 0x0 ("uncategorized").
     * Note that this is not a catch-all case -- the set of cases which may
     * result in this failure is specifically defined by the architecture.
     */
    CP_ACCESS_TRAP_UNCATEGORIZED = 2,
    /* As CP_ACCESS_TRAP, but for traps directly to EL2 or EL3 */
    CP_ACCESS_TRAP_EL2 = 3,
    CP_ACCESS_TRAP_EL3 = 4,
    /* As CP_ACCESS_UNCATEGORIZED, but for traps directly to EL2 or EL3 */
    CP_ACCESS_TRAP_UNCATEGORIZED_EL2 = 5,
    CP_ACCESS_TRAP_UNCATEGORIZED_EL3 = 6,
    /* Access fails and results in an exception syndrome for an FP access,
     * trapped directly to EL2 or EL3
     */
    CP_ACCESS_TRAP_FP_EL2 = 7,
    CP_ACCESS_TRAP_FP_EL3 = 8,
} CPAccessResult;

/* Access functions for coprocessor registers. These cannot fail and
 * may not raise exceptions.
 */
typedef uint64_t CPReadFn(CPUARMState *env, const ARMCPRegInfo *opaque);
typedef void CPWriteFn(CPUARMState *env, const ARMCPRegInfo *opaque,
                       uint64_t value);
/* Access permission check functions for coprocessor registers. */
typedef CPAccessResult CPAccessFn(CPUARMState *env,
                                  const ARMCPRegInfo *opaque,
                                  bool isread);
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
     * For AArch64-visible registers, opc0 is also used.
     * Since there are no "coprocessors" in AArch64, cp is purely used as a
     * way to distinguish (for KVM's benefit) guest-visible system registers
     * from demuxed ones provided to preserve the "no side effects on
     * KVM register read/write from QEMU" semantics. cp==0x13 is guest
     * visible (to match KVM's encoding); cp==0 will be converted to
     * cp==0x13 when the ARMCPRegInfo is registered, for convenience.
     */
    uint8_t cp;
    uint8_t crn;
    uint8_t crm;
    uint8_t opc0;
    uint8_t opc1;
    uint8_t opc2;
    /* Execution state in which this register is visible: ARM_CP_STATE_* */
    int state;
    /* Register type: ARM_CP_* bits/values */
    int type;
    /* Access rights: PL*_[RW] */
    int access;
    /* Security state: ARM_CP_SECSTATE_* bits/values */
    int secure;
    /* The opaque pointer passed to define_arm_cp_regs_with_opaque() when
     * this register was defined: can be used to hand data through to the
     * register read/write functions, since they are passed the ARMCPRegInfo*.
     */
    void *opaque;
    /* Value of this register, if it is ARM_CP_CONST. Otherwise, if
     * fieldoffset is non-zero, the reset value of the register.
     */
    uint64_t resetvalue;
    /* Offset of the field in CPUARMState for this register.
     *
     * This is not needed if either:
     *  1. type is ARM_CP_CONST or one of the ARM_CP_SPECIALs
     *  2. both readfn and writefn are specified
     */
    ptrdiff_t fieldoffset; /* offsetof(CPUARMState, field) */

    /* Offsets of the secure and non-secure fields in CPUARMState for the
     * register if it is banked.  These fields are only used during the static
     * registration of a register.  During hashing the bank associated
     * with a given security state is copied to fieldoffset which is used from
     * there on out.
     *
     * It is expected that register definitions use either fieldoffset or
     * bank_fieldoffsets in the definition but not both.  It is also expected
     * that both bank offsets are set when defining a banked register.  This
     * use indicates that a register is banked.
     */
    ptrdiff_t bank_fieldoffsets[2];

    /* Function for making any access checks for this register in addition to
     * those specified by the 'access' permissions bits. If NULL, no extra
     * checks required. The access check is performed at runtime, not at
     * translate time.
     */
    CPAccessFn *accessfn;
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
    /* Function for doing a "raw" read; used when we need to copy
     * coprocessor state to the kernel for KVM or out for
     * migration. This only needs to be provided if there is also a
     * readfn and it has side effects (for instance clear-on-read bits).
     */
    CPReadFn *raw_readfn;
    /* Function for doing a "raw" write; used when we need to copy KVM
     * kernel coprocessor state into userspace, or for inbound
     * migration. This only needs to be provided if there is also a
     * writefn and it masks out "unwritable" bits or has write-one-to-clear
     * or similar behaviour.
     */
    CPWriteFn *raw_writefn;
    /* Function for resetting the register. If NULL, then reset will be done
     * by writing resetvalue to the field specified in fieldoffset. If
     * fieldoffset is 0 then no reset will be done.
     */
    CPResetFn *resetfn;

    /*
     * "Original" writefn and readfn.
     * For ARMv8.1-VHE register aliases, we overwrite the read/write
     * accessor functions of various EL1/EL0 to perform the runtime
     * check for which sysreg should actually be modified, and then
     * forwards the operation.  Before overwriting the accessors,
     * the original function is copied here, so that accesses that
     * really do go to the EL1/EL0 version proceed normally.
     * (The corresponding EL2 register is linked via opaque.)
     */
    CPReadFn *orig_readfn;
    CPWriteFn *orig_writefn;
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
const ARMCPRegInfo *get_arm_cp_reginfo(GHashTable *cpregs, uint32_t encoded_cp);

/*
 * Definition of an ARM co-processor register as viewed from
 * userspace. This is used for presenting sanitised versions of
 * registers to userspace when emulating the Linux AArch64 CPU
 * ID/feature ABI (advertised as HWCAP_CPUID).
 */
typedef struct ARMCPRegUserSpaceInfo {
    /* Name of register */
    const char *name;

    /* Is the name actually a glob pattern */
    bool is_glob;

    /* Only some bits are exported to user space */
    uint64_t exported_bits;

    /* Fixed bits are applied after the mask */
    uint64_t fixed_bits;
} ARMCPRegUserSpaceInfo;

#define REGUSERINFO_SENTINEL { .name = NULL }

void modify_arm_cp_regs(ARMCPRegInfo *regs, const ARMCPRegUserSpaceInfo *mods);

/* CPWriteFn that can be used to implement writes-ignored behaviour */
void arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value);
/* CPReadFn that can be used for read-as-zero behaviour */
uint64_t arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri);

/* CPResetFn that does nothing, for use if no reset is required even
 * if fieldoffset is non zero.
 */
void arm_cp_reset_ignore(CPUARMState *env, const ARMCPRegInfo *opaque);

/* Return true if this reginfo struct's field in the cpu state struct
 * is 64 bits wide.
 */
static inline bool cpreg_field_is_64bit(const ARMCPRegInfo *ri)
{
    return (ri->state == ARM_CP_STATE_AA64) || (ri->type & ARM_CP_64BIT);
}

static inline bool cp_access_ok(int current_el,
                                const ARMCPRegInfo *ri, int isread)
{
    return (ri->access >> ((current_el * 2) + isread)) & 1;
}

/* Raw read of a coprocessor register (as needed for migration, etc) */
uint64_t read_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri);

/**
 * write_list_to_cpustate
 * @cpu: ARMCPU
 *
 * For each register listed in the ARMCPU cpreg_indexes list, write
 * its value from the cpreg_values list into the ARMCPUState structure.
 * This updates TCG's working data structures from KVM data or
 * from incoming migration state.
 *
 * Returns: true if all register values were updated correctly,
 * false if some register was unknown or could not be written.
 * Note that we do not stop early on failure -- we will attempt
 * writing all registers in the list.
 */
bool write_list_to_cpustate(ARMCPU *cpu);

/**
 * write_cpustate_to_list:
 * @cpu: ARMCPU
 * @kvm_sync: true if this is for syncing back to KVM
 *
 * For each register listed in the ARMCPU cpreg_indexes list, write
 * its value from the ARMCPUState structure into the cpreg_values list.
 * This is used to copy info from TCG's working data structures into
 * KVM or for outbound migration.
 *
 * @kvm_sync is true if we are doing this in order to sync the
 * register state back to KVM. In this case we will only update
 * values in the list if the previous list->cpustate sync actually
 * successfully wrote the CPU state. Otherwise we will keep the value
 * that is in the list.
 *
 * Returns: true if all register values were read correctly,
 * false if some register was unknown or could not be read.
 * Note that we do not stop early on failure -- we will attempt
 * reading all registers in the list.
 */
bool write_cpustate_to_list(ARMCPU *cpu, bool kvm_sync);

#define ARM_CPUID_TI915T      0x54029152
#define ARM_CPUID_TI925T      0x54029252

#define ARM_CPU_TYPE_SUFFIX "-" TYPE_ARM_CPU
#define ARM_CPU_TYPE_NAME(name) (name ARM_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_ARM_CPU

#define TYPE_ARM_HOST_CPU "host-" TYPE_ARM_CPU

#define cpu_list arm_cpu_list

/* ARM has the following "translation regimes" (as the ARM ARM calls them):
 *
 * If EL3 is 64-bit:
 *  + NonSecure EL1 & 0 stage 1
 *  + NonSecure EL1 & 0 stage 2
 *  + NonSecure EL2
 *  + NonSecure EL2 & 0   (ARMv8.1-VHE)
 *  + Secure EL1 & 0
 *  + Secure EL3
 * If EL3 is 32-bit:
 *  + NonSecure PL1 & 0 stage 1
 *  + NonSecure PL1 & 0 stage 2
 *  + NonSecure PL2
 *  + Secure PL0
 *  + Secure PL1
 * (reminder: for 32 bit EL3, Secure PL1 is *EL3*, not EL1.)
 *
 * For QEMU, an mmu_idx is not quite the same as a translation regime because:
 *  1. we need to split the "EL1 & 0" and "EL2 & 0" regimes into two mmu_idxes,
 *     because they may differ in access permissions even if the VA->PA map is
 *     the same
 *  2. we want to cache in our TLB the full VA->IPA->PA lookup for a stage 1+2
 *     translation, which means that we have one mmu_idx that deals with two
 *     concatenated translation regimes [this sort of combined s1+2 TLB is
 *     architecturally permitted]
 *  3. we don't need to allocate an mmu_idx to translations that we won't be
 *     handling via the TLB. The only way to do a stage 1 translation without
 *     the immediate stage 2 translation is via the ATS or AT system insns,
 *     which can be slow-pathed and always do a page table walk.
 *     The only use of stage 2 translations is either as part of an s1+2
 *     lookup or when loading the descriptors during a stage 1 page table walk,
 *     and in both those cases we don't use the TLB.
 *  4. we can also safely fold together the "32 bit EL3" and "64 bit EL3"
 *     translation regimes, because they map reasonably well to each other
 *     and they can't both be active at the same time.
 *  5. we want to be able to use the TLB for accesses done as part of a
 *     stage1 page table walk, rather than having to walk the stage2 page
 *     table over and over.
 *  6. we need separate EL1/EL2 mmu_idx for handling the Privileged Access
 *     Never (PAN) bit within PSTATE.
 *
 * This gives us the following list of cases:
 *
 * NS EL0 EL1&0 stage 1+2 (aka NS PL0)
 * NS EL1 EL1&0 stage 1+2 (aka NS PL1)
 * NS EL1 EL1&0 stage 1+2 +PAN
 * NS EL0 EL2&0
 * NS EL2 EL2&0
 * NS EL2 EL2&0 +PAN
 * NS EL2 (aka NS PL2)
 * S EL0 EL1&0 (aka S PL0)
 * S EL1 EL1&0 (not used if EL3 is 32 bit)
 * S EL1 EL1&0 +PAN
 * S EL3 (aka S PL1)
 *
 * for a total of 11 different mmu_idx.
 *
 * R profile CPUs have an MPU, but can use the same set of MMU indexes
 * as A profile. They only need to distinguish NS EL0 and NS EL1 (and
 * NS EL2 if we ever model a Cortex-R52).
 *
 * M profile CPUs are rather different as they do not have a true MMU.
 * They have the following different MMU indexes:
 *  User
 *  Privileged
 *  User, execution priority negative (ie the MPU HFNMIENA bit may apply)
 *  Privileged, execution priority negative (ditto)
 * If the CPU supports the v8M Security Extension then there are also:
 *  Secure User
 *  Secure Privileged
 *  Secure User, execution priority negative
 *  Secure Privileged, execution priority negative
 *
 * The ARMMMUIdx and the mmu index value used by the core QEMU TLB code
 * are not quite the same -- different CPU types (most notably M profile
 * vs A/R profile) would like to use MMU indexes with different semantics,
 * but since we don't ever need to use all of those in a single CPU we
 * can avoid having to set NB_MMU_MODES to "total number of A profile MMU
 * modes + total number of M profile MMU modes". The lower bits of
 * ARMMMUIdx are the core TLB mmu index, and the higher bits are always
 * the same for any particular CPU.
 * Variables of type ARMMUIdx are always full values, and the core
 * index values are in variables of type 'int'.
 *
 * Our enumeration includes at the end some entries which are not "true"
 * mmu_idx values in that they don't have corresponding TLBs and are only
 * valid for doing slow path page table walks.
 *
 * The constant names here are patterned after the general style of the names
 * of the AT/ATS operations.
 * The values used are carefully arranged to make mmu_idx => EL lookup easy.
 * For M profile we arrange them to have a bit for priv, a bit for negpri
 * and a bit for secure.
 */
#define ARM_MMU_IDX_A     0x10  /* A profile */
#define ARM_MMU_IDX_NOTLB 0x20  /* does not have a TLB */
#define ARM_MMU_IDX_M     0x40  /* M profile */

/* Meanings of the bits for A profile mmu idx values */
#define ARM_MMU_IDX_A_NS     0x8

/* Meanings of the bits for M profile mmu idx values */
#define ARM_MMU_IDX_M_PRIV   0x1
#define ARM_MMU_IDX_M_NEGPRI 0x2
#define ARM_MMU_IDX_M_S      0x4  /* Secure */

#define ARM_MMU_IDX_TYPE_MASK \
    (ARM_MMU_IDX_A | ARM_MMU_IDX_M | ARM_MMU_IDX_NOTLB)
#define ARM_MMU_IDX_COREIDX_MASK 0xf

typedef enum ARMMMUIdx {
    /*
     * A-profile.
     */
    ARMMMUIdx_SE10_0     =  0 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE20_0     =  1 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE10_1     =  2 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE20_2     =  3 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE10_1_PAN =  4 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE20_2_PAN =  5 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE2        =  6 | ARM_MMU_IDX_A,
    ARMMMUIdx_SE3        =  7 | ARM_MMU_IDX_A,

    ARMMMUIdx_E10_0     = ARMMMUIdx_SE10_0 | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E20_0     = ARMMMUIdx_SE20_0 | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E10_1     = ARMMMUIdx_SE10_1 | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E20_2     = ARMMMUIdx_SE20_2 | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E10_1_PAN = ARMMMUIdx_SE10_1_PAN | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E20_2_PAN = ARMMMUIdx_SE20_2_PAN | ARM_MMU_IDX_A_NS,
    ARMMMUIdx_E2        = ARMMMUIdx_SE2 | ARM_MMU_IDX_A_NS,

    /*
     * These are not allocated TLBs and are used only for AT system
     * instructions or for the first stage of an S12 page table walk.
     */
    ARMMMUIdx_Stage1_E0 = 0 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1 = 1 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1_PAN = 2 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_SE0 = 3 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_SE1 = 4 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_SE1_PAN = 5 | ARM_MMU_IDX_NOTLB,
    /*
     * Not allocated a TLB: used only for second stage of an S12 page
     * table walk, or for descriptor loads during first stage of an S1
     * page table walk. Note that if we ever want to have a TLB for this
     * then various TLB flush insns which currently are no-ops or flush
     * only stage 1 MMU indexes will need to change to flush stage 2.
     */
    ARMMMUIdx_Stage2     = 6 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage2_S   = 7 | ARM_MMU_IDX_NOTLB,

    /*
     * M-profile.
     */
    ARMMMUIdx_MUser = ARM_MMU_IDX_M,
    ARMMMUIdx_MPriv = ARM_MMU_IDX_M | ARM_MMU_IDX_M_PRIV,
    ARMMMUIdx_MUserNegPri = ARMMMUIdx_MUser | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MPrivNegPri = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MSUser = ARMMMUIdx_MUser | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPriv = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSUserNegPri = ARMMMUIdx_MUserNegPri | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPrivNegPri = ARMMMUIdx_MPrivNegPri | ARM_MMU_IDX_M_S,
} ARMMMUIdx;

/*
 * Bit macros for the core-mmu-index values for each index,
 * for use when calling tlb_flush_by_mmuidx() and friends.
 */
#define TO_CORE_BIT(NAME) \
    ARMMMUIdxBit_##NAME = 1 << (ARMMMUIdx_##NAME & ARM_MMU_IDX_COREIDX_MASK)

typedef enum ARMMMUIdxBit {
    TO_CORE_BIT(E10_0),
    TO_CORE_BIT(E20_0),
    TO_CORE_BIT(E10_1),
    TO_CORE_BIT(E10_1_PAN),
    TO_CORE_BIT(E2),
    TO_CORE_BIT(E20_2),
    TO_CORE_BIT(E20_2_PAN),
    TO_CORE_BIT(SE10_0),
    TO_CORE_BIT(SE20_0),
    TO_CORE_BIT(SE10_1),
    TO_CORE_BIT(SE20_2),
    TO_CORE_BIT(SE10_1_PAN),
    TO_CORE_BIT(SE20_2_PAN),
    TO_CORE_BIT(SE2),
    TO_CORE_BIT(SE3),

    TO_CORE_BIT(MUser),
    TO_CORE_BIT(MPriv),
    TO_CORE_BIT(MUserNegPri),
    TO_CORE_BIT(MPrivNegPri),
    TO_CORE_BIT(MSUser),
    TO_CORE_BIT(MSPriv),
    TO_CORE_BIT(MSUserNegPri),
    TO_CORE_BIT(MSPrivNegPri),
} ARMMMUIdxBit;

#undef TO_CORE_BIT

#define MMU_USER_IDX 0

/* Indexes used when registering address spaces with cpu_address_space_init */
typedef enum ARMASIdx {
    ARMASIdx_NS = 0,
    ARMASIdx_S = 1,
    ARMASIdx_TagNS = 2,
    ARMASIdx_TagS = 3,
} ARMASIdx;

/* Return the Exception Level targeted by debug exceptions. */
static inline int arm_debug_target_el(CPUARMState *env)
{
    bool secure = arm_is_secure(env);
    bool route_to_el2 = false;

    if (arm_is_el2_enabled(env)) {
        route_to_el2 = env->cp15.hcr_el2 & HCR_TGE ||
                       env->cp15.mdcr_el2 & MDCR_TDE;
    }

    if (route_to_el2) {
        return 2;
    } else if (arm_feature(env, ARM_FEATURE_EL3) &&
               !arm_el_is_aa64(env, 3) && secure) {
        return 3;
    } else {
        return 1;
    }
}

static inline bool arm_v7m_csselr_razwi(ARMCPU *cpu)
{
    /* If all the CLIDR.Ctypem bits are 0 there are no caches, and
     * CSSELR is RAZ/WI.
     */
    return (cpu->clidr & R_V7M_CLIDR_CTYPE_ALL_MASK) != 0;
}

/* See AArch64.GenerateDebugExceptionsFrom() in ARM ARM pseudocode */
static inline bool aa64_generate_debug_exceptions(CPUARMState *env)
{
    int cur_el = arm_current_el(env);
    int debug_el;

    if (cur_el == 3) {
        return false;
    }

    /* MDCR_EL3.SDD disables debug events from Secure state */
    if (arm_is_secure_below_el3(env)
        && extract32(env->cp15.mdcr_el3, 16, 1)) {
        return false;
    }

    /*
     * Same EL to same EL debug exceptions need MDSCR_KDE enabled
     * while not masking the (D)ebug bit in DAIF.
     */
    debug_el = arm_debug_target_el(env);

    if (cur_el == debug_el) {
        return extract32(env->cp15.mdscr_el1, 13, 1)
            && !(env->daif & PSTATE_D);
    }

    /* Otherwise the debug target needs to be a higher EL */
    return debug_el > cur_el;
}

static inline bool aa32_generate_debug_exceptions(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el == 0 && arm_el_is_aa64(env, 1)) {
        return aa64_generate_debug_exceptions(env);
    }

    if (arm_is_secure(env)) {
        int spd;

        if (el == 0 && (env->cp15.sder & 1)) {
            /* SDER.SUIDEN means debug exceptions from Secure EL0
             * are always enabled. Otherwise they are controlled by
             * SDCR.SPD like those from other Secure ELs.
             */
            return true;
        }

        spd = extract32(env->cp15.mdcr_el3, 14, 2);
        switch (spd) {
        case 1:
            /* SPD == 0b01 is reserved, but behaves as 0b00. */
        case 0:
            /* For 0b00 we return true if external secure invasive debug
             * is enabled. On real hardware this is controlled by external
             * signals to the core. QEMU always permits debug, and behaves
             * as if DBGEN, SPIDEN, NIDEN and SPNIDEN are all tied high.
             */
            return true;
        case 2:
            return false;
        case 3:
            return true;
        }
    }

    return el != 2;
}

/* Return true if debugging exceptions are currently enabled.
 * This corresponds to what in ARM ARM pseudocode would be
 *    if UsingAArch32() then
 *        return AArch32.GenerateDebugExceptions()
 *    else
 *        return AArch64.GenerateDebugExceptions()
 * We choose to push the if() down into this function for clarity,
 * since the pseudocode has it at all callsites except for the one in
 * CheckSoftwareStep(), where it is elided because both branches would
 * always return the same value.
 */
static inline bool arm_generate_debug_exceptions(CPUARMState *env)
{
    if (env->aarch64) {
        return aa64_generate_debug_exceptions(env);
    } else {
        return aa32_generate_debug_exceptions(env);
    }
}

/* Is single-stepping active? (Note that the "is EL_D AArch64?" check
 * implicitly means this always returns false in pre-v8 CPUs.)
 */
static inline bool arm_singlestep_active(CPUARMState *env)
{
    return extract32(env->cp15.mdscr_el1, 0, 1)
        && arm_el_is_aa64(env, arm_debug_target_el(env))
        && arm_generate_debug_exceptions(env);
}

static inline bool arm_sctlr_b(CPUARMState *env)
{
    return
        /* We need not implement SCTLR.ITD in user-mode emulation, so
         * let linux-user ignore the fact that it conflicts with SCTLR_B.
         * This lets people run BE32 binaries with "-cpu any".
         */
#ifndef CONFIG_USER_ONLY
        !arm_feature(env, ARM_FEATURE_V7) &&
#endif
        (env->cp15.sctlr_el[1] & SCTLR_B) != 0;
}

uint64_t arm_sctlr(CPUARMState *env, int el);

static inline bool arm_cpu_data_is_big_endian_a32(CPUARMState *env,
                                                  bool sctlr_b)
{
#ifdef CONFIG_USER_ONLY
    /*
     * In system mode, BE32 is modelled in line with the
     * architecture (as word-invariant big-endianness), where loads
     * and stores are done little endian but from addresses which
     * are adjusted by XORing with the appropriate constant. So the
     * endianness to use for the raw data access is not affected by
     * SCTLR.B.
     * In user mode, however, we model BE32 as byte-invariant
     * big-endianness (because user-only code cannot tell the
     * difference), and so we need to use a data access endianness
     * that depends on SCTLR.B.
     */
    if (sctlr_b) {
        return true;
    }
#endif
    /* In 32bit endianness is determined by looking at CPSR's E bit */
    return env->uncached_cpsr & CPSR_E;
}

static inline bool arm_cpu_data_is_big_endian_a64(int el, uint64_t sctlr)
{
    return sctlr & (el ? SCTLR_EE : SCTLR_E0E);
}

/* Return true if the processor is in big-endian mode. */
static inline bool arm_cpu_data_is_big_endian(CPUARMState *env)
{
    if (!is_a64(env)) {
        return arm_cpu_data_is_big_endian_a32(env, arm_sctlr_b(env));
    } else {
        int cur_el = arm_current_el(env);
        uint64_t sctlr = arm_sctlr(env, cur_el);
        return arm_cpu_data_is_big_endian_a64(cur_el, sctlr);
    }
}

#include "exec/cpu-all.h"

/*
 * We have more than 32-bits worth of state per TB, so we split the data
 * between tb->flags and tb->cs_base, which is otherwise unused for ARM.
 * We collect these two parts in CPUARMTBFlags where they are named
 * flags and flags2 respectively.
 *
 * The flags that are shared between all execution modes, TBFLAG_ANY,
 * are stored in flags.  The flags that are specific to a given mode
 * are stores in flags2.  Since cs_base is sized on the configured
 * address size, flags2 always has 64-bits for A64, and a minimum of
 * 32-bits for A32 and M32.
 *
 * The bits for 32-bit A-profile and M-profile partially overlap:
 *
 *  31         23         11 10             0
 * +-------------+----------+----------------+
 * |             |          |   TBFLAG_A32   |
 * | TBFLAG_AM32 |          +-----+----------+
 * |             |                |TBFLAG_M32|
 * +-------------+----------------+----------+
 *  31         23                6 5        0
 *
 * Unless otherwise noted, these bits are cached in env->hflags.
 */
FIELD(TBFLAG_ANY, AARCH64_STATE, 0, 1)
FIELD(TBFLAG_ANY, SS_ACTIVE, 1, 1)
FIELD(TBFLAG_ANY, PSTATE__SS, 2, 1)      /* Not cached. */
FIELD(TBFLAG_ANY, BE_DATA, 3, 1)
FIELD(TBFLAG_ANY, MMUIDX, 4, 4)
/* Target EL if we take a floating-point-disabled exception */
FIELD(TBFLAG_ANY, FPEXC_EL, 8, 2)
/* For A-profile only, target EL for debug exceptions.  */
FIELD(TBFLAG_ANY, DEBUG_TARGET_EL, 10, 2)
/* Memory operations require alignment: SCTLR_ELx.A or CCR.UNALIGN_TRP */
FIELD(TBFLAG_ANY, ALIGN_MEM, 12, 1)
FIELD(TBFLAG_ANY, PSTATE__IL, 13, 1)

/*
 * Bit usage when in AArch32 state, both A- and M-profile.
 */
FIELD(TBFLAG_AM32, CONDEXEC, 24, 8)      /* Not cached. */
FIELD(TBFLAG_AM32, THUMB, 23, 1)         /* Not cached. */

/*
 * Bit usage when in AArch32 state, for A-profile only.
 */
FIELD(TBFLAG_A32, VECLEN, 0, 3)         /* Not cached. */
FIELD(TBFLAG_A32, VECSTRIDE, 3, 2)     /* Not cached. */
/*
 * We store the bottom two bits of the CPAR as TB flags and handle
 * checks on the other bits at runtime. This shares the same bits as
 * VECSTRIDE, which is OK as no XScale CPU has VFP.
 * Not cached, because VECLEN+VECSTRIDE are not cached.
 */
FIELD(TBFLAG_A32, XSCALE_CPAR, 5, 2)
FIELD(TBFLAG_A32, VFPEN, 7, 1)         /* Partially cached, minus FPEXC. */
FIELD(TBFLAG_A32, SCTLR__B, 8, 1)      /* Cannot overlap with SCTLR_B */
FIELD(TBFLAG_A32, HSTR_ACTIVE, 9, 1)
/*
 * Indicates whether cp register reads and writes by guest code should access
 * the secure or nonsecure bank of banked registers; note that this is not
 * the same thing as the current security state of the processor!
 */
FIELD(TBFLAG_A32, NS, 10, 1)

/*
 * Bit usage when in AArch32 state, for M-profile only.
 */
/* Handler (ie not Thread) mode */
FIELD(TBFLAG_M32, HANDLER, 0, 1)
/* Whether we should generate stack-limit checks */
FIELD(TBFLAG_M32, STACKCHECK, 1, 1)
/* Set if FPCCR.LSPACT is set */
FIELD(TBFLAG_M32, LSPACT, 2, 1)                 /* Not cached. */
/* Set if we must create a new FP context */
FIELD(TBFLAG_M32, NEW_FP_CTXT_NEEDED, 3, 1)     /* Not cached. */
/* Set if FPCCR.S does not match current security state */
FIELD(TBFLAG_M32, FPCCR_S_WRONG, 4, 1)          /* Not cached. */
/* Set if MVE insns are definitely not predicated by VPR or LTPSIZE */
FIELD(TBFLAG_M32, MVE_NO_PRED, 5, 1)            /* Not cached. */

/*
 * Bit usage when in AArch64 state
 */
FIELD(TBFLAG_A64, TBII, 0, 2)
FIELD(TBFLAG_A64, SVEEXC_EL, 2, 2)
FIELD(TBFLAG_A64, ZCR_LEN, 4, 4)
FIELD(TBFLAG_A64, PAUTH_ACTIVE, 8, 1)
FIELD(TBFLAG_A64, BT, 9, 1)
FIELD(TBFLAG_A64, BTYPE, 10, 2)         /* Not cached. */
FIELD(TBFLAG_A64, TBID, 12, 2)
FIELD(TBFLAG_A64, UNPRIV, 14, 1)
FIELD(TBFLAG_A64, ATA, 15, 1)
FIELD(TBFLAG_A64, TCMA, 16, 2)
FIELD(TBFLAG_A64, MTE_ACTIVE, 18, 1)
FIELD(TBFLAG_A64, MTE0_ACTIVE, 19, 1)

/*
 * Helpers for using the above.
 */
#define DP_TBFLAG_ANY(DST, WHICH, VAL) \
    (DST.flags = FIELD_DP32(DST.flags, TBFLAG_ANY, WHICH, VAL))
#define DP_TBFLAG_A64(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_A64, WHICH, VAL))
#define DP_TBFLAG_A32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_A32, WHICH, VAL))
#define DP_TBFLAG_M32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_M32, WHICH, VAL))
#define DP_TBFLAG_AM32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_AM32, WHICH, VAL))

#define EX_TBFLAG_ANY(IN, WHICH)   FIELD_EX32(IN.flags, TBFLAG_ANY, WHICH)
#define EX_TBFLAG_A64(IN, WHICH)   FIELD_EX32(IN.flags2, TBFLAG_A64, WHICH)
#define EX_TBFLAG_A32(IN, WHICH)   FIELD_EX32(IN.flags2, TBFLAG_A32, WHICH)
#define EX_TBFLAG_M32(IN, WHICH)   FIELD_EX32(IN.flags2, TBFLAG_M32, WHICH)
#define EX_TBFLAG_AM32(IN, WHICH)  FIELD_EX32(IN.flags2, TBFLAG_AM32, WHICH)

/**
 * cpu_mmu_index:
 * @env: The cpu environment
 * @ifetch: True for code access, false for data access.
 *
 * Return the core mmu index for the current translation regime.
 * This function is used by generic TCG code paths.
 */
static inline int cpu_mmu_index(CPUARMState *env, bool ifetch)
{
    return EX_TBFLAG_ANY(env->hflags, MMUIDX);
}

static inline bool bswap_code(bool sctlr_b)
{
#ifdef CONFIG_USER_ONLY
    /* BE8 (SCTLR.B = 0, TARGET_WORDS_BIGENDIAN = 1) is mixed endian.
     * The invalid combination SCTLR.B=1/CPSR.E=1/TARGET_WORDS_BIGENDIAN=0
     * would also end up as a mixed-endian mode with BE code, LE data.
     */
    return
#ifdef TARGET_WORDS_BIGENDIAN
        1 ^
#endif
        sctlr_b;
#else
    /* All code access in ARM is little endian, and there are no loaders
     * doing swaps that need to be reversed
     */
    return 0;
#endif
}

#ifdef CONFIG_USER_ONLY
static inline bool arm_cpu_bswap_data(CPUARMState *env)
{
    return
#ifdef TARGET_WORDS_BIGENDIAN
       1 ^
#endif
       arm_cpu_data_is_big_endian(env);
}
#endif

void cpu_get_tb_cpu_state(CPUARMState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *flags);

enum {
    QEMU_PSCI_CONDUIT_DISABLED = 0,
    QEMU_PSCI_CONDUIT_SMC = 1,
    QEMU_PSCI_CONDUIT_HVC = 2,
};

#ifndef CONFIG_USER_ONLY
/* Return the address space index to use for a memory access */
static inline int arm_asidx_from_attrs(CPUState *cs, MemTxAttrs attrs)
{
    return attrs.secure ? ARMASIdx_S : ARMASIdx_NS;
}

/* Return the AddressSpace to use for a memory access
 * (which depends on whether the access is S or NS, and whether
 * the board gave us a separate AddressSpace for S accesses).
 */
static inline AddressSpace *arm_addressspace(CPUState *cs, MemTxAttrs attrs)
{
    return cpu_get_address_space(cs, arm_asidx_from_attrs(cs, attrs));
}
#endif

/**
 * arm_register_pre_el_change_hook:
 * Register a hook function which will be called immediately before this
 * CPU changes exception level or mode. The hook function will be
 * passed a pointer to the ARMCPU and the opaque data pointer passed
 * to this function when the hook was registered.
 *
 * Note that if a pre-change hook is called, any registered post-change hooks
 * are guaranteed to subsequently be called.
 */
void arm_register_pre_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook,
                                 void *opaque);
/**
 * arm_register_el_change_hook:
 * Register a hook function which will be called immediately after this
 * CPU changes exception level or mode. The hook function will be
 * passed a pointer to the ARMCPU and the opaque data pointer passed
 * to this function when the hook was registered.
 *
 * Note that any registered hooks registered here are guaranteed to be called
 * if pre-change hooks have been.
 */
void arm_register_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook, void
        *opaque);

/**
 * arm_rebuild_hflags:
 * Rebuild the cached TBFLAGS for arbitrary changed processor state.
 */
void arm_rebuild_hflags(CPUARMState *env);

/**
 * aa32_vfp_dreg:
 * Return a pointer to the Dn register within env in 32-bit mode.
 */
static inline uint64_t *aa32_vfp_dreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno >> 1].d[regno & 1];
}

/**
 * aa32_vfp_qreg:
 * Return a pointer to the Qn register within env in 32-bit mode.
 */
static inline uint64_t *aa32_vfp_qreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno].d[0];
}

/**
 * aa64_vfp_qreg:
 * Return a pointer to the Qn register within env in 64-bit mode.
 */
static inline uint64_t *aa64_vfp_qreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno].d[0];
}

/* Shared between translate-sve.c and sve_helper.c.  */
extern const uint64_t pred_esz_masks[4];

/* Helper for the macros below, validating the argument type. */
static inline MemTxAttrs *typecheck_memtxattrs(MemTxAttrs *x)
{
    return x;
}

/*
 * Lvalue macros for ARM TLB bits that we must cache in the TCG TLB.
 * Using these should be a bit more self-documenting than using the
 * generic target bits directly.
 */
#define arm_tlb_bti_gp(x) (typecheck_memtxattrs(x)->target_tlb_bit0)
#define arm_tlb_mte_tagged(x) (typecheck_memtxattrs(x)->target_tlb_bit1)

/*
 * AArch64 usage of the PAGE_TARGET_* bits for linux-user.
 */
#define PAGE_BTI  PAGE_TARGET_1
#define PAGE_MTE  PAGE_TARGET_2

#ifdef TARGET_TAGGED_ADDRESSES
/**
 * cpu_untagged_addr:
 * @cs: CPU context
 * @x: tagged address
 *
 * Remove any address tag from @x.  This is explicitly related to the
 * linux syscall TIF_TAGGED_ADDR setting, not TBI in general.
 *
 * There should be a better place to put this, but we need this in
 * include/exec/cpu_ldst.h, and not some place linux-user specific.
 */
static inline target_ulong cpu_untagged_addr(CPUState *cs, target_ulong x)
{
    ARMCPU *cpu = ARM_CPU(cs);
    if (cpu->env.tagged_addr_enable) {
        /*
         * TBI is enabled for userspace but not kernelspace addresses.
         * Only clear the tag if bit 55 is clear.
         */
        x &= sextract64(x, 0, 56);
    }
    return x;
}
#endif

/*
 * Naming convention for isar_feature functions:
 * Functions which test 32-bit ID registers should have _aa32_ in
 * their name. Functions which test 64-bit ID registers should have
 * _aa64_ in their name. These must only be used in code where we
 * know for certain that the CPU has AArch32 or AArch64 respectively
 * or where the correct answer for a CPU which doesn't implement that
 * CPU state is "false" (eg when generating A32 or A64 code, if adding
 * system registers that are specific to that CPU state, for "should
 * we let this system register bit be set" tests where the 32-bit
 * flavour of the register doesn't have the bit, and so on).
 * Functions which simply ask "does this feature exist at all" have
 * _any_ in their name, and always return the logical OR of the _aa64_
 * and the _aa32_ function.
 */

/*
 * 32-bit feature tests via id registers.
 */
static inline bool isar_feature_aa32_thumb_div(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar0, ID_ISAR0, DIVIDE) != 0;
}

static inline bool isar_feature_aa32_arm_div(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar0, ID_ISAR0, DIVIDE) > 1;
}

static inline bool isar_feature_aa32_lob(const ARMISARegisters *id)
{
    /* (M-profile) low-overhead loops and branch future */
    return FIELD_EX32(id->id_isar0, ID_ISAR0, CMPBRANCH) >= 3;
}

static inline bool isar_feature_aa32_jazelle(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar1, ID_ISAR1, JAZELLE) != 0;
}

static inline bool isar_feature_aa32_aes(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, AES) != 0;
}

static inline bool isar_feature_aa32_pmull(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, AES) > 1;
}

static inline bool isar_feature_aa32_sha1(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, SHA1) != 0;
}

static inline bool isar_feature_aa32_sha2(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, SHA2) != 0;
}

static inline bool isar_feature_aa32_crc32(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, CRC32) != 0;
}

static inline bool isar_feature_aa32_rdm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, RDM) != 0;
}

static inline bool isar_feature_aa32_vcma(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, VCMA) != 0;
}

static inline bool isar_feature_aa32_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, JSCVT) != 0;
}

static inline bool isar_feature_aa32_dp(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, DP) != 0;
}

static inline bool isar_feature_aa32_fhm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, FHM) != 0;
}

static inline bool isar_feature_aa32_sb(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, SB) != 0;
}

static inline bool isar_feature_aa32_predinv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, SPECRES) != 0;
}

static inline bool isar_feature_aa32_bf16(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, BF16) != 0;
}

static inline bool isar_feature_aa32_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, I8MM) != 0;
}

static inline bool isar_feature_aa32_ras(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr0, ID_PFR0, RAS) != 0;
}

static inline bool isar_feature_aa32_mprofile(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr1, ID_PFR1, MPROGMOD) != 0;
}

static inline bool isar_feature_aa32_m_sec_state(const ARMISARegisters *id)
{
    /*
     * Return true if M-profile state handling insns
     * (VSCCLRM, CLRM, FPCTX access insns) are implemented
     */
    return FIELD_EX32(id->id_pfr1, ID_PFR1, SECURITY) >= 3;
}

static inline bool isar_feature_aa32_fp16_arith(const ARMISARegisters *id)
{
    /* Sadly this is encoded differently for A-profile and M-profile */
    if (isar_feature_aa32_mprofile(id)) {
        return FIELD_EX32(id->mvfr1, MVFR1, FP16) > 0;
    } else {
        return FIELD_EX32(id->mvfr1, MVFR1, FPHP) >= 3;
    }
}

static inline bool isar_feature_aa32_mve(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) > 0;
}

static inline bool isar_feature_aa32_mve_fp(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) >= 2;
}

static inline bool isar_feature_aa32_vfp_simd(const ARMISARegisters *id)
{
    /*
     * Return true if either VFP or SIMD is implemented.
     * In this case, a minimum of VFP w/ D0-D15.
     */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) > 0;
}

static inline bool isar_feature_aa32_simd_r32(const ARMISARegisters *id)
{
    /* Return true if D16-D31 are implemented */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) >= 2;
}

static inline bool isar_feature_aa32_fpshvec(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr0, MVFR0, FPSHVEC) > 0;
}

static inline bool isar_feature_aa32_fpsp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) > 0;
}

static inline bool isar_feature_aa32_fpsp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) >= 2;
}

static inline bool isar_feature_aa32_fpdp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) > 0;
}

static inline bool isar_feature_aa32_fpdp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) >= 2;
}

static inline bool isar_feature_aa32_vfp(const ARMISARegisters *id)
{
    return isar_feature_aa32_fpsp_v2(id) || isar_feature_aa32_fpdp_v2(id);
}

/*
 * We always set the FP and SIMD FP16 fields to indicate identical
 * levels of support (assuming SIMD is implemented at all), so
 * we only need one set of accessors.
 */
static inline bool isar_feature_aa32_fp16_spconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 0;
}

static inline bool isar_feature_aa32_fp16_dpconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 1;
}

/*
 * Note that this ID register field covers both VFP and Neon FMAC,
 * so should usually be tested in combination with some other
 * check that confirms the presence of whichever of VFP or Neon is
 * relevant, to avoid accidentally enabling a Neon feature on
 * a VFP-no-Neon core or vice-versa.
 */
static inline bool isar_feature_aa32_simdfmac(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, SIMDFMAC) != 0;
}

static inline bool isar_feature_aa32_vsel(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 1;
}

static inline bool isar_feature_aa32_vcvt_dr(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 2;
}

static inline bool isar_feature_aa32_vrint(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 3;
}

static inline bool isar_feature_aa32_vminmaxnm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 4;
}

static inline bool isar_feature_aa32_pxn(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr0, ID_MMFR0, VMSA) >= 4;
}

static inline bool isar_feature_aa32_pan(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr3, ID_MMFR3, PAN) != 0;
}

static inline bool isar_feature_aa32_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr3, ID_MMFR3, PAN) >= 2;
}

static inline bool isar_feature_aa32_pmu_8_1(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) >= 4 &&
        FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_pmu_8_4(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) >= 5 &&
        FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_hpd(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, HPDS) != 0;
}

static inline bool isar_feature_aa32_ac2(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, AC2) != 0;
}

static inline bool isar_feature_aa32_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, CCIDX) != 0;
}

static inline bool isar_feature_aa32_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, XNX) != 0;
}

static inline bool isar_feature_aa32_dit(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr0, ID_PFR0, DIT) != 0;
}

static inline bool isar_feature_aa32_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr2, ID_PFR2, SSBS) != 0;
}

/*
 * 64-bit feature tests via id registers.
 */
static inline bool isar_feature_aa64_aes(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, AES) != 0;
}

static inline bool isar_feature_aa64_pmull(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, AES) > 1;
}

static inline bool isar_feature_aa64_sha1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA1) != 0;
}

static inline bool isar_feature_aa64_sha256(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA2) != 0;
}

static inline bool isar_feature_aa64_sha512(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA2) > 1;
}

static inline bool isar_feature_aa64_crc32(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, CRC32) != 0;
}

static inline bool isar_feature_aa64_atomics(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, ATOMIC) != 0;
}

static inline bool isar_feature_aa64_rdm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, RDM) != 0;
}

static inline bool isar_feature_aa64_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sm3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SM3) != 0;
}

static inline bool isar_feature_aa64_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SM4) != 0;
}

static inline bool isar_feature_aa64_dp(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, DP) != 0;
}

static inline bool isar_feature_aa64_fhm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, FHM) != 0;
}

static inline bool isar_feature_aa64_condm_4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TS) != 0;
}

static inline bool isar_feature_aa64_condm_5(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TS) >= 2;
}

static inline bool isar_feature_aa64_rndr(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, RNDR) != 0;
}

static inline bool isar_feature_aa64_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, JSCVT) != 0;
}

static inline bool isar_feature_aa64_fcma(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, FCMA) != 0;
}

static inline bool isar_feature_aa64_pauth(const ARMISARegisters *id)
{
    /*
     * Return true if any form of pauth is enabled, as this
     * predicate controls migration of the 128-bit keys.
     */
    return (id->id_aa64isar1 &
            (FIELD_DP64(0, ID_AA64ISAR1, APA, 0xf) |
             FIELD_DP64(0, ID_AA64ISAR1, API, 0xf) |
             FIELD_DP64(0, ID_AA64ISAR1, GPA, 0xf) |
             FIELD_DP64(0, ID_AA64ISAR1, GPI, 0xf))) != 0;
}

static inline bool isar_feature_aa64_pauth_arch(const ARMISARegisters *id)
{
    /*
     * Return true if pauth is enabled with the architected QARMA algorithm.
     * QEMU will always set APA+GPA to the same value.
     */
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, APA) != 0;
}

static inline bool isar_feature_aa64_tlbirange(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TLB) == 2;
}

static inline bool isar_feature_aa64_tlbios(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TLB) != 0;
}

static inline bool isar_feature_aa64_sb(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, SB) != 0;
}

static inline bool isar_feature_aa64_predinv(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, SPECRES) != 0;
}

static inline bool isar_feature_aa64_frint(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, FRINTTS) != 0;
}

static inline bool isar_feature_aa64_dcpop(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, DPB) != 0;
}

static inline bool isar_feature_aa64_dcpodp(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, DPB) >= 2;
}

static inline bool isar_feature_aa64_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, BF16) != 0;
}

static inline bool isar_feature_aa64_fp_simd(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically.  */
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, FP) != 0xf;
}

static inline bool isar_feature_aa64_fp16(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically wrt FP16.  */
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, FP) == 1;
}

static inline bool isar_feature_aa64_aa32(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, EL0) >= 2;
}

static inline bool isar_feature_aa64_aa32_el1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, EL1) >= 2;
}

static inline bool isar_feature_aa64_sve(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, SVE) != 0;
}

static inline bool isar_feature_aa64_sel2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, SEL2) != 0;
}

static inline bool isar_feature_aa64_vh(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, VH) != 0;
}

static inline bool isar_feature_aa64_lor(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, LO) != 0;
}

static inline bool isar_feature_aa64_pan(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, PAN) != 0;
}

static inline bool isar_feature_aa64_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, PAN) >= 2;
}

static inline bool isar_feature_aa64_uao(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, UAO) != 0;
}

static inline bool isar_feature_aa64_st(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, ST) != 0;
}

static inline bool isar_feature_aa64_bti(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, BT) != 0;
}

static inline bool isar_feature_aa64_mte_insn_reg(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, MTE) != 0;
}

static inline bool isar_feature_aa64_mte(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, MTE) >= 2;
}

static inline bool isar_feature_aa64_pmu_8_1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) >= 4 &&
        FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_pmu_8_4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) >= 5 &&
        FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_rcpc_8_3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, LRCPC) != 0;
}

static inline bool isar_feature_aa64_rcpc_8_4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, LRCPC) >= 2;
}

static inline bool isar_feature_aa64_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, I8MM) != 0;
}

static inline bool isar_feature_aa64_tgran4_lpa2(const ARMISARegisters *id)
{
    return FIELD_SEX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4) >= 1;
}

static inline bool isar_feature_aa64_tgran4_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran4_lpa2(id));
}

static inline bool isar_feature_aa64_tgran16_lpa2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16) >= 2;
}

static inline bool isar_feature_aa64_tgran16_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran16_lpa2(id));
}

static inline bool isar_feature_aa64_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, CCIDX) != 0;
}

static inline bool isar_feature_aa64_lva(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, VARANGE) != 0;
}

static inline bool isar_feature_aa64_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, XNX) != 0;
}

static inline bool isar_feature_aa64_dit(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, DIT) != 0;
}

static inline bool isar_feature_aa64_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, SSBS) != 0;
}

static inline bool isar_feature_aa64_sve2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SVEVER) != 0;
}

static inline bool isar_feature_aa64_sve2_aes(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, AES) != 0;
}

static inline bool isar_feature_aa64_sve2_pmull128(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, AES) >= 2;
}

static inline bool isar_feature_aa64_sve2_bitperm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, BITPERM) != 0;
}

static inline bool isar_feature_aa64_sve_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, BFLOAT16) != 0;
}

static inline bool isar_feature_aa64_sve2_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sve2_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SM4) != 0;
}

static inline bool isar_feature_aa64_sve_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, I8MM) != 0;
}

static inline bool isar_feature_aa64_sve_f32mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, F32MM) != 0;
}

static inline bool isar_feature_aa64_sve_f64mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, F64MM) != 0;
}

/*
 * Feature tests for "does this exist in either 32-bit or 64-bit?"
 */
static inline bool isar_feature_any_fp16(const ARMISARegisters *id)
{
    return isar_feature_aa64_fp16(id) || isar_feature_aa32_fp16_arith(id);
}

static inline bool isar_feature_any_predinv(const ARMISARegisters *id)
{
    return isar_feature_aa64_predinv(id) || isar_feature_aa32_predinv(id);
}

static inline bool isar_feature_any_pmu_8_1(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmu_8_1(id) || isar_feature_aa32_pmu_8_1(id);
}

static inline bool isar_feature_any_pmu_8_4(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmu_8_4(id) || isar_feature_aa32_pmu_8_4(id);
}

static inline bool isar_feature_any_ccidx(const ARMISARegisters *id)
{
    return isar_feature_aa64_ccidx(id) || isar_feature_aa32_ccidx(id);
}

static inline bool isar_feature_any_tts2uxn(const ARMISARegisters *id)
{
    return isar_feature_aa64_tts2uxn(id) || isar_feature_aa32_tts2uxn(id);
}

/*
 * Forward to the above feature tests given an ARMCPU pointer.
 */
#define cpu_isar_feature(name, cpu) \
    ({ ARMCPU *cpu_ = (cpu); isar_feature_##name(&cpu_->isar); })

#endif
