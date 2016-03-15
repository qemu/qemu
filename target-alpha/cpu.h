/*
 *  Alpha emulation cpu definitions for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#if !defined (__CPU_ALPHA_H__)
#define __CPU_ALPHA_H__

#include "qemu-common.h"
#include "cpu-qom.h"

#define TARGET_LONG_BITS 64
#define ALIGNED_ONLY

#define CPUArchState struct CPUAlphaState

#include "exec/cpu-defs.h"

#include "fpu/softfloat.h"

#define ICACHE_LINE_SIZE 32
#define DCACHE_LINE_SIZE 32

#define TARGET_PAGE_BITS 13

#ifdef CONFIG_USER_ONLY
/* ??? The kernel likes to give addresses in high memory.  If the host has
   more virtual address space than the guest, this can lead to impossible
   allocations.  Honor the long-standing assumption that only kernel addrs
   are negative, but otherwise allow allocations anywhere.  This could lead
   to tricky emulation problems for programs doing tagged addressing, but
   that's far fewer than encounter the impossible allocation problem.  */
#define TARGET_PHYS_ADDR_SPACE_BITS  63
#define TARGET_VIRT_ADDR_SPACE_BITS  63
#else
/* ??? EV4 has 34 phys addr bits, EV5 has 40, EV6 has 44.  */
#define TARGET_PHYS_ADDR_SPACE_BITS  44
#define TARGET_VIRT_ADDR_SPACE_BITS  (30 + TARGET_PAGE_BITS)
#endif

/* Alpha major type */
enum {
    ALPHA_EV3  = 1,
    ALPHA_EV4  = 2,
    ALPHA_SIM  = 3,
    ALPHA_LCA  = 4,
    ALPHA_EV5  = 5, /* 21164 */
    ALPHA_EV45 = 6, /* 21064A */
    ALPHA_EV56 = 7, /* 21164A */
};

/* EV4 minor type */
enum {
    ALPHA_EV4_2 = 0,
    ALPHA_EV4_3 = 1,
};

/* LCA minor type */
enum {
    ALPHA_LCA_1 = 1, /* 21066 */
    ALPHA_LCA_2 = 2, /* 20166 */
    ALPHA_LCA_3 = 3, /* 21068 */
    ALPHA_LCA_4 = 4, /* 21068 */
    ALPHA_LCA_5 = 5, /* 21066A */
    ALPHA_LCA_6 = 6, /* 21068A */
};

/* EV5 minor type */
enum {
    ALPHA_EV5_1 = 1, /* Rev BA, CA */
    ALPHA_EV5_2 = 2, /* Rev DA, EA */
    ALPHA_EV5_3 = 3, /* Pass 3 */
    ALPHA_EV5_4 = 4, /* Pass 3.2 */
    ALPHA_EV5_5 = 5, /* Pass 4 */
};

/* EV45 minor type */
enum {
    ALPHA_EV45_1 = 1, /* Pass 1 */
    ALPHA_EV45_2 = 2, /* Pass 1.1 */
    ALPHA_EV45_3 = 3, /* Pass 2 */
};

/* EV56 minor type */
enum {
    ALPHA_EV56_1 = 1, /* Pass 1 */
    ALPHA_EV56_2 = 2, /* Pass 2 */
};

enum {
    IMPLVER_2106x = 0, /* EV4, EV45 & LCA45 */
    IMPLVER_21164 = 1, /* EV5, EV56 & PCA45 */
    IMPLVER_21264 = 2, /* EV6, EV67 & EV68x */
    IMPLVER_21364 = 3, /* EV7 & EV79 */
};

enum {
    AMASK_BWX      = 0x00000001,
    AMASK_FIX      = 0x00000002,
    AMASK_CIX      = 0x00000004,
    AMASK_MVI      = 0x00000100,
    AMASK_TRAP     = 0x00000200,
    AMASK_PREFETCH = 0x00001000,
};

enum {
    VAX_ROUND_NORMAL = 0,
    VAX_ROUND_CHOPPED,
};

enum {
    IEEE_ROUND_NORMAL = 0,
    IEEE_ROUND_DYNAMIC,
    IEEE_ROUND_PLUS,
    IEEE_ROUND_MINUS,
    IEEE_ROUND_CHOPPED,
};

/* IEEE floating-point operations encoding */
/* Trap mode */
enum {
    FP_TRAP_I   = 0x0,
    FP_TRAP_U   = 0x1,
    FP_TRAP_S  = 0x4,
    FP_TRAP_SU  = 0x5,
    FP_TRAP_SUI = 0x7,
};

/* Rounding mode */
enum {
    FP_ROUND_CHOPPED = 0x0,
    FP_ROUND_MINUS   = 0x1,
    FP_ROUND_NORMAL  = 0x2,
    FP_ROUND_DYNAMIC = 0x3,
};

/* FPCR bits -- right-shifted 32 so we can use a uint32_t.  */
#define FPCR_SUM                (1U << (63 - 32))
#define FPCR_INED               (1U << (62 - 32))
#define FPCR_UNFD               (1U << (61 - 32))
#define FPCR_UNDZ               (1U << (60 - 32))
#define FPCR_DYN_SHIFT          (58 - 32)
#define FPCR_DYN_CHOPPED        (0U << FPCR_DYN_SHIFT)
#define FPCR_DYN_MINUS          (1U << FPCR_DYN_SHIFT)
#define FPCR_DYN_NORMAL         (2U << FPCR_DYN_SHIFT)
#define FPCR_DYN_PLUS           (3U << FPCR_DYN_SHIFT)
#define FPCR_DYN_MASK           (3U << FPCR_DYN_SHIFT)
#define FPCR_IOV                (1U << (57 - 32))
#define FPCR_INE                (1U << (56 - 32))
#define FPCR_UNF                (1U << (55 - 32))
#define FPCR_OVF                (1U << (54 - 32))
#define FPCR_DZE                (1U << (53 - 32))
#define FPCR_INV                (1U << (52 - 32))
#define FPCR_OVFD               (1U << (51 - 32))
#define FPCR_DZED               (1U << (50 - 32))
#define FPCR_INVD               (1U << (49 - 32))
#define FPCR_DNZ                (1U << (48 - 32))
#define FPCR_DNOD               (1U << (47 - 32))
#define FPCR_STATUS_MASK        (FPCR_IOV | FPCR_INE | FPCR_UNF \
                                 | FPCR_OVF | FPCR_DZE | FPCR_INV)

/* The silly software trap enables implemented by the kernel emulation.
   These are more or less architecturally required, since the real hardware
   has read-as-zero bits in the FPCR when the features aren't implemented.
   For the purposes of QEMU, we pretend the FPCR can hold everything.  */
#define SWCR_TRAP_ENABLE_INV    (1U << 1)
#define SWCR_TRAP_ENABLE_DZE    (1U << 2)
#define SWCR_TRAP_ENABLE_OVF    (1U << 3)
#define SWCR_TRAP_ENABLE_UNF    (1U << 4)
#define SWCR_TRAP_ENABLE_INE    (1U << 5)
#define SWCR_TRAP_ENABLE_DNO    (1U << 6)
#define SWCR_TRAP_ENABLE_MASK   ((1U << 7) - (1U << 1))

#define SWCR_MAP_DMZ            (1U << 12)
#define SWCR_MAP_UMZ            (1U << 13)
#define SWCR_MAP_MASK           (SWCR_MAP_DMZ | SWCR_MAP_UMZ)

#define SWCR_STATUS_INV         (1U << 17)
#define SWCR_STATUS_DZE         (1U << 18)
#define SWCR_STATUS_OVF         (1U << 19)
#define SWCR_STATUS_UNF         (1U << 20)
#define SWCR_STATUS_INE         (1U << 21)
#define SWCR_STATUS_DNO         (1U << 22)
#define SWCR_STATUS_MASK        ((1U << 23) - (1U << 17))

#define SWCR_MASK  (SWCR_TRAP_ENABLE_MASK | SWCR_MAP_MASK | SWCR_STATUS_MASK)

/* MMU modes definitions */

/* Alpha has 5 MMU modes: PALcode, kernel, executive, supervisor, and user.
   The Unix PALcode only exposes the kernel and user modes; presumably
   executive and supervisor are used by VMS.

   PALcode itself uses physical mode for code and kernel mode for data;
   there are PALmode instructions that can access data via physical mode
   or via an os-installed "alternate mode", which is one of the 4 above.

   QEMU does not currently properly distinguish between code/data when
   looking up addresses.  To avoid having to address this issue, our
   emulated PALcode will cheat and use the KSEG mapping for its code+data
   rather than physical addresses.

   Moreover, we're only emulating Unix PALcode, and not attempting VMS.

   All of which allows us to drop all but kernel and user modes.
   Elide the unused MMU modes to save space.  */

#define NB_MMU_MODES 2

#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_KERNEL_IDX   0
#define MMU_USER_IDX     1

typedef struct CPUAlphaState CPUAlphaState;

struct CPUAlphaState {
    uint64_t ir[31];
    float64 fir[31];
    uint64_t pc;
    uint64_t unique;
    uint64_t lock_addr;
    uint64_t lock_st_addr;
    uint64_t lock_value;

    /* The FPCR, and disassembled portions thereof.  */
    uint32_t fpcr;
    uint32_t fpcr_exc_enable;
    float_status fp_status;
    uint8_t fpcr_dyn_round;
    uint8_t fpcr_flush_to_zero;

    /* The Internal Processor Registers.  Some of these we assume always
       exist for use in user-mode.  */
    uint8_t ps;
    uint8_t intr_flag;
    uint8_t pal_mode;
    uint8_t fen;

    uint32_t pcc_ofs;

    /* These pass data from the exception logic in the translator and
       helpers to the OS entry point.  This is used for both system
       emulation and user-mode.  */
    uint64_t trap_arg0;
    uint64_t trap_arg1;
    uint64_t trap_arg2;

#if !defined(CONFIG_USER_ONLY)
    /* The internal data required by our emulation of the Unix PALcode.  */
    uint64_t exc_addr;
    uint64_t palbr;
    uint64_t ptbr;
    uint64_t vptptr;
    uint64_t sysval;
    uint64_t usp;
    uint64_t shadow[8];
    uint64_t scratch[24];
#endif

    /* This alarm doesn't exist in real hardware; we wish it did.  */
    uint64_t alarm_expire;

    /* Those resources are used only in QEMU core */
    CPU_COMMON

    int error_code;

    uint32_t features;
    uint32_t amask;
    int implver;
};

/**
 * AlphaCPU:
 * @env: #CPUAlphaState
 *
 * An Alpha CPU.
 */
struct AlphaCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUAlphaState env;

    /* This alarm doesn't exist in real hardware; we wish it did.  */
    QEMUTimer *alarm_timer;
};

static inline AlphaCPU *alpha_env_get_cpu(CPUAlphaState *env)
{
    return container_of(env, AlphaCPU, env);
}

#define ENV_GET_CPU(e) CPU(alpha_env_get_cpu(e))

#define ENV_OFFSET offsetof(AlphaCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_alpha_cpu;
#endif

void alpha_cpu_do_interrupt(CPUState *cpu);
bool alpha_cpu_exec_interrupt(CPUState *cpu, int int_req);
void alpha_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                          int flags);
hwaddr alpha_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int alpha_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int alpha_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void alpha_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                   int is_write, int is_user, uintptr_t retaddr);

#define cpu_list alpha_cpu_list
#define cpu_exec cpu_alpha_exec
#define cpu_signal_handler cpu_alpha_signal_handler

#include "exec/cpu-all.h"

enum {
    FEATURE_ASN    = 0x00000001,
    FEATURE_SPS    = 0x00000002,
    FEATURE_VIRBND = 0x00000004,
    FEATURE_TBCHK  = 0x00000008,
};

enum {
    EXCP_RESET,
    EXCP_MCHK,
    EXCP_SMP_INTERRUPT,
    EXCP_CLK_INTERRUPT,
    EXCP_DEV_INTERRUPT,
    EXCP_MMFAULT,
    EXCP_UNALIGN,
    EXCP_OPCDEC,
    EXCP_ARITH,
    EXCP_FEN,
    EXCP_CALL_PAL,
    /* For Usermode emulation.  */
    EXCP_STL_C,
    EXCP_STQ_C,
};

/* Alpha-specific interrupt pending bits.  */
#define CPU_INTERRUPT_TIMER	CPU_INTERRUPT_TGT_EXT_0
#define CPU_INTERRUPT_SMP	CPU_INTERRUPT_TGT_EXT_1
#define CPU_INTERRUPT_MCHK	CPU_INTERRUPT_TGT_EXT_2

/* OSF/1 Page table bits.  */
enum {
    PTE_VALID = 0x0001,
    PTE_FOR   = 0x0002,  /* used for page protection (fault on read) */
    PTE_FOW   = 0x0004,  /* used for page protection (fault on write) */
    PTE_FOE   = 0x0008,  /* used for page protection (fault on exec) */
    PTE_ASM   = 0x0010,
    PTE_KRE   = 0x0100,
    PTE_URE   = 0x0200,
    PTE_KWE   = 0x1000,
    PTE_UWE   = 0x2000
};

/* Hardware interrupt (entInt) constants.  */
enum {
    INT_K_IP,
    INT_K_CLK,
    INT_K_MCHK,
    INT_K_DEV,
    INT_K_PERF,
};

/* Memory management (entMM) constants.  */
enum {
    MM_K_TNV,
    MM_K_ACV,
    MM_K_FOR,
    MM_K_FOE,
    MM_K_FOW
};

/* Arithmetic exception (entArith) constants.  */
enum {
    EXC_M_SWC = 1,      /* Software completion */
    EXC_M_INV = 2,      /* Invalid operation */
    EXC_M_DZE = 4,      /* Division by zero */
    EXC_M_FOV = 8,      /* Overflow */
    EXC_M_UNF = 16,     /* Underflow */
    EXC_M_INE = 32,     /* Inexact result */
    EXC_M_IOV = 64      /* Integer Overflow */
};

/* Processor status constants.  */
enum {
    /* Low 3 bits are interrupt mask level.  */
    PS_INT_MASK = 7,

    /* Bits 4 and 5 are the mmu mode.  The VMS PALcode uses all 4 modes;
       The Unix PALcode only uses bit 4.  */
    PS_USER_MODE = 8
};

static inline int cpu_mmu_index(CPUAlphaState *env, bool ifetch)
{
    if (env->pal_mode) {
        return MMU_KERNEL_IDX;
    } else if (env->ps & PS_USER_MODE) {
        return MMU_USER_IDX;
    } else {
        return MMU_KERNEL_IDX;
    }
}

enum {
    IR_V0   = 0,
    IR_T0   = 1,
    IR_T1   = 2,
    IR_T2   = 3,
    IR_T3   = 4,
    IR_T4   = 5,
    IR_T5   = 6,
    IR_T6   = 7,
    IR_T7   = 8,
    IR_S0   = 9,
    IR_S1   = 10,
    IR_S2   = 11,
    IR_S3   = 12,
    IR_S4   = 13,
    IR_S5   = 14,
    IR_S6   = 15,
    IR_FP   = IR_S6,
    IR_A0   = 16,
    IR_A1   = 17,
    IR_A2   = 18,
    IR_A3   = 19,
    IR_A4   = 20,
    IR_A5   = 21,
    IR_T8   = 22,
    IR_T9   = 23,
    IR_T10  = 24,
    IR_T11  = 25,
    IR_RA   = 26,
    IR_T12  = 27,
    IR_PV   = IR_T12,
    IR_AT   = 28,
    IR_GP   = 29,
    IR_SP   = 30,
    IR_ZERO = 31,
};

void alpha_translate_init(void);

AlphaCPU *cpu_alpha_init(const char *cpu_model);

#define cpu_init(cpu_model) CPU(cpu_alpha_init(cpu_model))

void alpha_cpu_list(FILE *f, fprintf_function cpu_fprintf);
int cpu_alpha_exec(CPUState *cpu);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_alpha_signal_handler(int host_signum, void *pinfo,
                             void *puc);
int alpha_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw,
                               int mmu_idx);
void do_restore_state(CPUAlphaState *, uintptr_t retaddr);
void QEMU_NORETURN dynamic_excp(CPUAlphaState *, uintptr_t, int, int);
void QEMU_NORETURN arith_excp(CPUAlphaState *, uintptr_t, int, uint64_t);

uint64_t cpu_alpha_load_fpcr (CPUAlphaState *env);
void cpu_alpha_store_fpcr (CPUAlphaState *env, uint64_t val);
uint64_t cpu_alpha_load_gr(CPUAlphaState *env, unsigned reg);
void cpu_alpha_store_gr(CPUAlphaState *env, unsigned reg, uint64_t val);
#ifndef CONFIG_USER_ONLY
QEMU_NORETURN void alpha_cpu_unassigned_access(CPUState *cpu, hwaddr addr,
                                               bool is_write, bool is_exec,
                                               int unused, unsigned size);
#endif

/* Bits in TB->FLAGS that control how translation is processed.  */
enum {
    TB_FLAGS_PAL_MODE = 1,
    TB_FLAGS_FEN = 2,
    TB_FLAGS_USER_MODE = 8,

    TB_FLAGS_AMASK_SHIFT = 4,
    TB_FLAGS_AMASK_BWX = AMASK_BWX << TB_FLAGS_AMASK_SHIFT,
    TB_FLAGS_AMASK_FIX = AMASK_FIX << TB_FLAGS_AMASK_SHIFT,
    TB_FLAGS_AMASK_CIX = AMASK_CIX << TB_FLAGS_AMASK_SHIFT,
    TB_FLAGS_AMASK_MVI = AMASK_MVI << TB_FLAGS_AMASK_SHIFT,
    TB_FLAGS_AMASK_TRAP = AMASK_TRAP << TB_FLAGS_AMASK_SHIFT,
    TB_FLAGS_AMASK_PREFETCH = AMASK_PREFETCH << TB_FLAGS_AMASK_SHIFT,
};

static inline void cpu_get_tb_cpu_state(CPUAlphaState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *pflags)
{
    int flags = 0;

    *pc = env->pc;
    *cs_base = 0;

    if (env->pal_mode) {
        flags = TB_FLAGS_PAL_MODE;
    } else {
        flags = env->ps & PS_USER_MODE;
    }
    if (env->fen) {
        flags |= TB_FLAGS_FEN;
    }
    flags |= env->amask << TB_FLAGS_AMASK_SHIFT;

    *pflags = flags;
}

#endif /* !defined (__CPU_ALPHA_H__) */
