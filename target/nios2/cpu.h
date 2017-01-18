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
#ifndef CPU_NIOS2_H
#define CPU_NIOS2_H

#include "qemu/osdep.h"
#include "qemu-common.h"

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUNios2State

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"
#include "qom/cpu.h"
struct CPUNios2State;
typedef struct CPUNios2State CPUNios2State;
#if !defined(CONFIG_USER_ONLY)
#include "mmu.h"
#endif

#define TYPE_NIOS2_CPU "nios2-cpu"

#define NIOS2_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(Nios2CPUClass, (klass), TYPE_NIOS2_CPU)
#define NIOS2_CPU(obj) \
    OBJECT_CHECK(Nios2CPU, (obj), TYPE_NIOS2_CPU)
#define NIOS2_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(Nios2CPUClass, (obj), TYPE_NIOS2_CPU)

/**
 * Nios2CPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * A Nios2 CPU model.
 */
typedef struct Nios2CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} Nios2CPUClass;

#define TARGET_HAS_ICE 1

/* Configuration options for Nios II */
#define RESET_ADDRESS         0x00000000
#define EXCEPTION_ADDRESS     0x00000004
#define FAST_TLB_MISS_ADDRESS 0x00000008


/* GP regs + CR regs + PC */
#define NUM_CORE_REGS (32 + 32 + 1)

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
#define CR_BASE  32
#define CR_STATUS    (CR_BASE + 0)
#define   CR_STATUS_PIE  (1 << 0)
#define   CR_STATUS_U    (1 << 1)
#define   CR_STATUS_EH   (1 << 2)
#define   CR_STATUS_IH   (1 << 3)
#define   CR_STATUS_IL   (63 << 4)
#define   CR_STATUS_CRS  (63 << 10)
#define   CR_STATUS_PRS  (63 << 16)
#define   CR_STATUS_NMI  (1 << 22)
#define   CR_STATUS_RSIE (1 << 23)
#define CR_ESTATUS   (CR_BASE + 1)
#define CR_BSTATUS   (CR_BASE + 2)
#define CR_IENABLE   (CR_BASE + 3)
#define CR_IPENDING  (CR_BASE + 4)
#define CR_CPUID     (CR_BASE + 5)
#define CR_CTL6      (CR_BASE + 6)
#define CR_EXCEPTION (CR_BASE + 7)
#define CR_PTEADDR   (CR_BASE + 8)
#define   CR_PTEADDR_PTBASE_SHIFT 22
#define   CR_PTEADDR_PTBASE_MASK  (0x3FF << CR_PTEADDR_PTBASE_SHIFT)
#define   CR_PTEADDR_VPN_SHIFT    2
#define   CR_PTEADDR_VPN_MASK     (0xFFFFF << CR_PTEADDR_VPN_SHIFT)
#define CR_TLBACC    (CR_BASE + 9)
#define   CR_TLBACC_IGN_SHIFT 25
#define   CR_TLBACC_IGN_MASK  (0x7F << CR_TLBACC_IGN_SHIFT)
#define   CR_TLBACC_C         (1 << 24)
#define   CR_TLBACC_R         (1 << 23)
#define   CR_TLBACC_W         (1 << 22)
#define   CR_TLBACC_X         (1 << 21)
#define   CR_TLBACC_G         (1 << 20)
#define   CR_TLBACC_PFN_MASK  0x000FFFFF
#define CR_TLBMISC   (CR_BASE + 10)
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
#define CR_ENCINJ    (CR_BASE + 11)
#define CR_BADADDR   (CR_BASE + 12)
#define CR_CONFIG    (CR_BASE + 13)
#define CR_MPUBASE   (CR_BASE + 14)
#define CR_MPUACC    (CR_BASE + 15)

/* Other registers */
#define R_PC         64

/* Exceptions */
#define EXCP_BREAK    -1
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

#define NB_MMU_MODES 2

struct CPUNios2State {
    uint32_t regs[NUM_CORE_REGS];

#if !defined(CONFIG_USER_ONLY)
    Nios2MMU mmu;

    uint32_t irq_pending;
#endif

    CPU_COMMON
};

/**
 * Nios2CPU:
 * @env: #CPUNios2State
 *
 * A Nios2 CPU.
 */
typedef struct Nios2CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNios2State env;
    bool mmu_present;
    uint32_t pid_num_bits;
    uint32_t tlb_num_ways;
    uint32_t tlb_num_entries;

    /* Addresses that are hard-coded in the FPGA build settings */
    uint32_t reset_addr;
    uint32_t exception_addr;
    uint32_t fast_tlb_miss_addr;
} Nios2CPU;

static inline Nios2CPU *nios2_env_get_cpu(CPUNios2State *env)
{
    return NIOS2_CPU(container_of(env, Nios2CPU, env));
}

#define ENV_GET_CPU(e) CPU(nios2_env_get_cpu(e))

#define ENV_OFFSET offsetof(Nios2CPU, env)

void nios2_tcg_init(void);
Nios2CPU *cpu_nios2_init(const char *cpu_model);
void nios2_cpu_do_interrupt(CPUState *cs);
int cpu_nios2_signal_handler(int host_signum, void *pinfo, void *puc);
void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUNios2State *env);
void nios2_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                          int flags);
hwaddr nios2_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void nios2_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                   MMUAccessType access_type,
                                   int mmu_idx, uintptr_t retaddr);

qemu_irq *nios2_cpu_pic_init(Nios2CPU *cpu);
void nios2_check_interrupts(CPUNios2State *env);

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define cpu_init(cpu_model) CPU(cpu_nios2_init(cpu_model))

#define cpu_gen_code cpu_nios2_gen_code
#define cpu_signal_handler cpu_nios2_signal_handler

#define CPU_SAVE_VERSION 1

#define TARGET_PAGE_BITS 12

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_SUPERVISOR_IDX  0
#define MMU_USER_IDX        1

static inline int cpu_mmu_index(CPUNios2State *env, bool ifetch)
{
    return (env->regs[CR_STATUS] & CR_STATUS_U) ? MMU_USER_IDX :
                                                  MMU_SUPERVISOR_IDX;
}

int nios2_cpu_handle_mmu_fault(CPUState *env, vaddr address,
                               int rw, int mmu_idx);

static inline int cpu_interrupts_enabled(CPUNios2State *env)
{
    return env->regs[CR_STATUS] & CR_STATUS_PIE;
}

#include "exec/cpu-all.h"
#include "exec/exec-all.h"

static inline void cpu_get_tb_cpu_state(CPUNios2State *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->regs[R_PC];
    *cs_base = 0;
    *flags = (env->regs[CR_STATUS] & (CR_STATUS_EH | CR_STATUS_U));
}

#endif /* CPU_NIOS2_H */
