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
 * @parent_reset: The parent class' reset handler.
 *
 * A Nios2 CPU model.
 */
struct Nios2CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#define TARGET_HAS_ICE 1

/* Configuration options for Nios II */
#define RESET_ADDRESS         0x00000000
#define EXCEPTION_ADDRESS     0x00000004
#define FAST_TLB_MISS_ADDRESS 0x00000008

#define NUM_GP_REGS 32
#define NUM_CR_REGS 32

/* General purpose register aliases */
#define R_ZERO   0
#define R_AT     1
#define R_RET0   2
#define R_RET1   3
#define R_ARG0   4
#define R_ARG1   5
#define R_ARG2   6
#define R_ARG3   7
#define R_ET     24
#define R_BT     25
#define R_GP     26
#define R_SP     27
#define R_FP     28
#define R_EA     29
#define R_BA     30
#define R_RA     31

/* Control register aliases */
#define CR_STATUS        0

FIELD(CR_STATUS, PIE, 0, 1)
FIELD(CR_STATUS, U, 1, 1)
FIELD(CR_STATUS, EH, 2, 1)
FIELD(CR_STATUS, IH, 3, 1)
FIELD(CR_STATUS, IL, 4, 6)
FIELD(CR_STATUS, CRS, 10, 6)
FIELD(CR_STATUS, PRS, 16, 6)
FIELD(CR_STATUS, NMI, 22, 1)
FIELD(CR_STATUS, RSIE, 23, 1)

#define CR_STATUS_PIE    R_CR_STATUS_PIE_MASK
#define CR_STATUS_U      R_CR_STATUS_U_MASK
#define CR_STATUS_EH     R_CR_STATUS_EH_MASK
#define CR_STATUS_IH     R_CR_STATUS_IH_MASK
#define CR_STATUS_NMI    R_CR_STATUS_NMI_MASK
#define CR_STATUS_RSIE   R_CR_STATUS_RSIE_MASK

#define CR_ESTATUS       1
#define CR_BSTATUS       2
#define CR_IENABLE       3
#define CR_IPENDING      4
#define CR_CPUID         5
#define CR_CTL6          6
#define CR_EXCEPTION     7

FIELD(CR_EXCEPTION, CAUSE, 2, 5)
FIELD(CR_EXCEPTION, ECCFTL, 31, 1)

#define CR_PTEADDR       8
#define   CR_PTEADDR_PTBASE_SHIFT 22
#define   CR_PTEADDR_PTBASE_MASK  (0x3FF << CR_PTEADDR_PTBASE_SHIFT)
#define   CR_PTEADDR_VPN_SHIFT    2
#define   CR_PTEADDR_VPN_MASK     (0xFFFFF << CR_PTEADDR_VPN_SHIFT)
#define CR_TLBACC        9
#define   CR_TLBACC_IGN_SHIFT 25
#define   CR_TLBACC_IGN_MASK  (0x7F << CR_TLBACC_IGN_SHIFT)
#define   CR_TLBACC_C         (1 << 24)
#define   CR_TLBACC_R         (1 << 23)
#define   CR_TLBACC_W         (1 << 22)
#define   CR_TLBACC_X         (1 << 21)
#define   CR_TLBACC_G         (1 << 20)
#define   CR_TLBACC_PFN_MASK  0x000FFFFF
#define CR_TLBMISC       10
#define   CR_TLBMISC_WAY_SHIFT 20
#define   CR_TLBMISC_WAY_MASK  (0xF << CR_TLBMISC_WAY_SHIFT)
#define   CR_TLBMISC_RD        (1 << 19)
#define   CR_TLBMISC_WR        (1 << 18)
#define   CR_TLBMISC_PID_SHIFT 4
#define   CR_TLBMISC_PID_MASK  (0x3FFF << CR_TLBMISC_PID_SHIFT)
#define   CR_TLBMISC_DBL       (1 << 3)
#define   CR_TLBMISC_BAD       (1 << 2)
#define   CR_TLBMISC_PERM      (1 << 1)
#define   CR_TLBMISC_D         (1 << 0)
#define CR_ENCINJ        11
#define CR_BADADDR       12
#define CR_CONFIG        13
#define CR_MPUBASE       14
#define CR_MPUACC        15

/* Exceptions */
#define EXCP_BREAK    0x1000
#define EXCP_RESET    0
#define EXCP_PRESET   1
#define EXCP_IRQ      2
#define EXCP_TRAP     3
#define EXCP_UNIMPL   4
#define EXCP_ILLEGAL  5
#define EXCP_UNALIGN  6
#define EXCP_UNALIGND 7
#define EXCP_DIV      8
#define EXCP_SUPERA   9
#define EXCP_SUPERI   10
#define EXCP_SUPERD   11
#define EXCP_TLBD     12
#define EXCP_TLBX     13
#define EXCP_TLBR     14
#define EXCP_TLBW     15
#define EXCP_MPUI     16
#define EXCP_MPUD     17

#define CPU_INTERRUPT_NMI       CPU_INTERRUPT_TGT_EXT_3

struct CPUArchState {
    uint32_t regs[NUM_GP_REGS];
    uint32_t ctrl[NUM_CR_REGS];
    uint32_t pc;

#if !defined(CONFIG_USER_ONLY)
    Nios2MMU mmu;
#endif
    int error_code;
};

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

    bool mmu_present;
    uint32_t pid_num_bits;
    uint32_t tlb_num_ways;
    uint32_t tlb_num_entries;

    /* Addresses that are hard-coded in the FPGA build settings */
    uint32_t reset_addr;
    uint32_t exception_addr;
    uint32_t fast_tlb_miss_addr;
};


void nios2_tcg_init(void);
void nios2_cpu_do_interrupt(CPUState *cs);
void dump_mmu(CPUNios2State *env);
void nios2_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
hwaddr nios2_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
G_NORETURN void nios2_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                              MMUAccessType access_type, int mmu_idx,
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
bool nios2_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
#endif

typedef CPUNios2State CPUArchState;
typedef Nios2CPU ArchCPU;

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUNios2State *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = env->ctrl[CR_STATUS] & (CR_STATUS_EH | CR_STATUS_U);
}

#endif /* NIOS2_CPU_H */
