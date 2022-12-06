/*
 * Altera Nios II virtual CPU header
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef NIOS2_CPU_H
#define NIOS2_CPU_H

#include "exec/cpu-defs.h"
#include "hw/core/cpu.h"
#include "hw/registerfields.h"
#include "qom/object.h"

typedef struct CPUArchState CPUNios2State;
#if !defined(CONFIG_USER_ONLY)
#include "mmu.h"
#endif

#define TYPE_NIOS2_CPU "nios2-cpu"

OBJECT_DECLARE_CPU_TYPE(Nios2CPU, Nios2CPUClass, NIOS2_CPU)

/**
 * Nios2CPUClass:
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A Nios2 CPU model.
 */
struct Nios2CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define TARGET_HAS_ICE 1

/* Configuration options for Nios II */
#define RESET_ADDRESS         0x00000000
#define EXCEPTION_ADDRESS     0x00000004
#define FAST_TLB_MISS_ADDRESS 0x00000008

#define NUM_GP_REGS 32
#define NUM_CR_REGS 32

#ifndef CONFIG_USER_ONLY
/* 63 shadow register sets; index 0 is the primary register set. */
#define NUM_REG_SETS 64
#endif

/* General purpose register aliases */
enum {
    R_ZERO   = 0,
    R_AT     = 1,
    R_RET0   = 2,
    R_RET1   = 3,
    R_ARG0   = 4,
    R_ARG1   = 5,
    R_ARG2   = 6,
    R_ARG3   = 7,
    R_ET     = 24,
    R_BT     = 25,
    R_GP     = 26,
    R_SP     = 27,
    R_FP     = 28,
    R_EA     = 29,
    R_BA     = 30,
    R_SSTATUS = 30,
    R_RA     = 31,
};

/* Control register aliases */
enum {
    CR_STATUS        = 0,
    CR_ESTATUS       = 1,
    CR_BSTATUS       = 2,
    CR_IENABLE       = 3,
    CR_IPENDING      = 4,
    CR_CPUID         = 5,
    CR_EXCEPTION     = 7,
    CR_PTEADDR       = 8,
    CR_TLBACC        = 9,
    CR_TLBMISC       = 10,
    CR_ENCINJ        = 11,
    CR_BADADDR       = 12,
    CR_CONFIG        = 13,
    CR_MPUBASE       = 14,
    CR_MPUACC        = 15,
};

FIELD(CR_STATUS, PIE, 0, 1)
FIELD(CR_STATUS, U, 1, 1)
FIELD(CR_STATUS, EH, 2, 1)
FIELD(CR_STATUS, IH, 3, 1)
FIELD(CR_STATUS, IL, 4, 6)
FIELD(CR_STATUS, CRS, 10, 6)
FIELD(CR_STATUS, PRS, 16, 6)
FIELD(CR_STATUS, NMI, 22, 1)
FIELD(CR_STATUS, RSIE, 23, 1)
FIELD(CR_STATUS, SRS, 31, 1)  /* only in sstatus */

#define CR_STATUS_PIE    R_CR_STATUS_PIE_MASK
#define CR_STATUS_U      R_CR_STATUS_U_MASK
#define CR_STATUS_EH     R_CR_STATUS_EH_MASK
#define CR_STATUS_IH     R_CR_STATUS_IH_MASK
#define CR_STATUS_NMI    R_CR_STATUS_NMI_MASK
#define CR_STATUS_RSIE   R_CR_STATUS_RSIE_MASK
#define CR_STATUS_SRS    R_CR_STATUS_SRS_MASK

FIELD(CR_EXCEPTION, CAUSE, 2, 5)
FIELD(CR_EXCEPTION, ECCFTL, 31, 1)

FIELD(CR_PTEADDR, VPN, 2, 20)
FIELD(CR_PTEADDR, PTBASE, 22, 10)

FIELD(CR_TLBACC, PFN, 0, 20)
FIELD(CR_TLBACC, G, 20, 1)
FIELD(CR_TLBACC, X, 21, 1)
FIELD(CR_TLBACC, W, 22, 1)
FIELD(CR_TLBACC, R, 23, 1)
FIELD(CR_TLBACC, C, 24, 1)
FIELD(CR_TLBACC, IG, 25, 7)

#define CR_TLBACC_C      R_CR_TLBACC_C_MASK
#define CR_TLBACC_R      R_CR_TLBACC_R_MASK
#define CR_TLBACC_W      R_CR_TLBACC_W_MASK
#define CR_TLBACC_X      R_CR_TLBACC_X_MASK
#define CR_TLBACC_G      R_CR_TLBACC_G_MASK

FIELD(CR_TLBMISC, D, 0, 1)
FIELD(CR_TLBMISC, PERM, 1, 1)
FIELD(CR_TLBMISC, BAD, 2, 1)
FIELD(CR_TLBMISC, DBL, 3, 1)
FIELD(CR_TLBMISC, PID, 4, 14)
FIELD(CR_TLBMISC, WE, 18, 1)
FIELD(CR_TLBMISC, RD, 19, 1)
FIELD(CR_TLBMISC, WAY, 20, 4)
FIELD(CR_TLBMISC, EE, 24, 1)

#define CR_TLBMISC_EE    R_CR_TLBMISC_EE_MASK
#define CR_TLBMISC_RD    R_CR_TLBMISC_RD_MASK
#define CR_TLBMISC_WE    R_CR_TLBMISC_WE_MASK
#define CR_TLBMISC_DBL   R_CR_TLBMISC_DBL_MASK
#define CR_TLBMISC_BAD   R_CR_TLBMISC_BAD_MASK
#define CR_TLBMISC_PERM  R_CR_TLBMISC_PERM_MASK
#define CR_TLBMISC_D     R_CR_TLBMISC_D_MASK

/* Exceptions */
#define EXCP_BREAK    0x1000
#define EXCP_SEMIHOST 0x1001
#define EXCP_RESET    0
#define EXCP_PRESET   1
#define EXCP_IRQ      2
#define EXCP_TRAP     3
#define EXCP_UNIMPL   4
#define EXCP_ILLEGAL  5
#define EXCP_UNALIGN  6
#define EXCP_UNALIGND 7
#define EXCP_DIV      8
#define EXCP_SUPERA_X 9
#define EXCP_SUPERI   10
#define EXCP_SUPERA_D 11
#define EXCP_TLB_X    12
#define EXCP_TLB_D    (0x1000 | EXCP_TLB_X)
#define EXCP_PERM_X   13
#define EXCP_PERM_R   14
#define EXCP_PERM_W   15
#define EXCP_MPUI     16
#define EXCP_MPUD     17

struct CPUArchState {
#ifdef CONFIG_USER_ONLY
    uint32_t regs[NUM_GP_REGS];
#else
    uint32_t shadow_regs[NUM_REG_SETS][NUM_GP_REGS];
    /* Pointer into shadow_regs for the current register set. */
    uint32_t *regs;
#endif
    uint32_t ctrl[NUM_CR_REGS];
    uint32_t pc;

#if !defined(CONFIG_USER_ONLY)
    Nios2MMU mmu;
#endif
    int error_code;
};

typedef struct {
    uint32_t writable;
    uint32_t readonly;
} ControlRegState;

/**
 * Nios2CPU:
 * @env: #CPUNios2State
 *
 * A Nios2 CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUNios2State env;

    bool diverr_present;
    bool mmu_present;
    bool eic_present;

    uint32_t pid_num_bits;
    uint32_t tlb_num_ways;
    uint32_t tlb_num_entries;

    /* Addresses that are hard-coded in the FPGA build settings */
    uint32_t reset_addr;
    uint32_t exception_addr;
    uint32_t fast_tlb_miss_addr;

    /* Bits within each control register which are reserved or readonly. */
    ControlRegState cr_state[NUM_CR_REGS];

    /* External Interrupt Controller Interface */
    uint32_t rha; /* Requested handler address */
    uint32_t ril; /* Requested interrupt level */
    uint32_t rrs; /* Requested register set */
    bool rnmi;    /* Requested nonmaskable interrupt */
};


static inline bool nios2_cr_reserved(const ControlRegState *s)
{
    return (s->writable | s->readonly) == 0;
}

static inline void nios2_update_crs(CPUNios2State *env)
{
#ifndef CONFIG_USER_ONLY
    unsigned crs = FIELD_EX32(env->ctrl[CR_STATUS], CR_STATUS, CRS);
    env->regs = env->shadow_regs[crs];
#endif
}

void nios2_tcg_init(void);
void nios2_cpu_do_interrupt(CPUState *cs);
void dump_mmu(CPUNios2State *env);
void nios2_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
G_NORETURN void nios2_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                              MMUAccessType access_type, int mmu_idx,
                                              uintptr_t retaddr);
G_NORETURN void nios2_cpu_loop_exit_advance(CPUNios2State *env,
                                            uintptr_t retaddr);

void do_nios2_semihosting(CPUNios2State *env);

#define CPU_RESOLVING_TYPE TYPE_NIOS2_CPU

#define cpu_gen_code cpu_nios2_gen_code

#define CPU_SAVE_VERSION 1

/* MMU modes definitions */
#define MMU_SUPERVISOR_IDX  0
#define MMU_USER_IDX        1

static inline int cpu_mmu_index(CPUNios2State *env, bool ifetch)
{
    return (env->ctrl[CR_STATUS] & CR_STATUS_U) ? MMU_USER_IDX :
                                                  MMU_SUPERVISOR_IDX;
}

#ifndef CONFIG_USER_ONLY
hwaddr nios2_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
bool nios2_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
#endif

typedef CPUNios2State CPUArchState;
typedef Nios2CPU ArchCPU;

#include "exec/cpu-all.h"

FIELD(TBFLAGS, CRS0, 0, 1)  /* Set if CRS == 0. */
FIELD(TBFLAGS, U, 1, 1)     /* Overlaps CR_STATUS_U */
FIELD(TBFLAGS, R0_0, 2, 1)  /* Set if R0 == 0. */

static inline void cpu_get_tb_cpu_state(CPUNios2State *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    unsigned crs = FIELD_EX32(env->ctrl[CR_STATUS], CR_STATUS, CRS);

    *pc = env->pc;
    *cs_base = 0;
    *flags = (env->ctrl[CR_STATUS] & CR_STATUS_U)
           | (crs ? 0 : R_TBFLAGS_CRS0_MASK)
           | (env->regs[0] ? 0 : R_TBFLAGS_R0_0_MASK);
}

#endif /* NIOS2_CPU_H */
