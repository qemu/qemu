/*
 *  MicroBlaze virtual CPU header
 *
 *  Copyright (c) 2009 Edgar E. Iglesias
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_MICROBLAZE_H
#define CPU_MICROBLAZE_H

#include "qemu-common.h"
#include "cpu-qom.h"

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUMBState

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"
struct CPUMBState;
typedef struct CPUMBState CPUMBState;
#if !defined(CONFIG_USER_ONLY)
#include "mmu.h"
#endif

#define EXCP_MMU        1
#define EXCP_IRQ        2
#define EXCP_BREAK      3
#define EXCP_HW_BREAK   4
#define EXCP_HW_EXCP    5

/* MicroBlaze-specific interrupt pending bits.  */
#define CPU_INTERRUPT_NMI       CPU_INTERRUPT_TGT_EXT_3

/* Meanings of the MBCPU object's two inbound GPIO lines */
#define MB_CPU_IRQ 0
#define MB_CPU_FIR 1

/* Register aliases. R0 - R15 */
#define R_SP     1
#define SR_PC    0
#define SR_MSR   1
#define SR_EAR   3
#define SR_ESR   5
#define SR_FSR   7
#define SR_BTR   0xb
#define SR_EDR   0xd

/* MSR flags.  */
#define MSR_BE  (1<<0) /* 0x001 */
#define MSR_IE  (1<<1) /* 0x002 */
#define MSR_C   (1<<2) /* 0x004 */
#define MSR_BIP (1<<3) /* 0x008 */
#define MSR_FSL (1<<4) /* 0x010 */
#define MSR_ICE (1<<5) /* 0x020 */
#define MSR_DZ  (1<<6) /* 0x040 */
#define MSR_DCE (1<<7) /* 0x080 */
#define MSR_EE  (1<<8) /* 0x100 */
#define MSR_EIP (1<<9) /* 0x200 */
#define MSR_PVR (1<<10) /* 0x400 */
#define MSR_CC  (1<<31)

/* Machine State Register (MSR) Fields */
#define MSR_UM (1<<11) /* User Mode */
#define MSR_UMS        (1<<12) /* User Mode Save */
#define MSR_VM (1<<13) /* Virtual Mode */
#define MSR_VMS        (1<<14) /* Virtual Mode Save */

#define MSR_KERNEL      MSR_EE|MSR_VM
//#define MSR_USER     MSR_KERNEL|MSR_UM|MSR_IE
#define MSR_KERNEL_VMS  MSR_EE|MSR_VMS
//#define MSR_USER_VMS MSR_KERNEL_VMS|MSR_UMS|MSR_IE

/* Exception State Register (ESR) Fields */
#define          ESR_DIZ       (1<<11) /* Zone Protection */
#define          ESR_S         (1<<10) /* Store instruction */

#define          ESR_ESS_FSL_OFFSET     5

#define          ESR_EC_FSL             0
#define          ESR_EC_UNALIGNED_DATA  1
#define          ESR_EC_ILLEGAL_OP      2
#define          ESR_EC_INSN_BUS        3
#define          ESR_EC_DATA_BUS        4
#define          ESR_EC_DIVZERO         5
#define          ESR_EC_FPU             6
#define          ESR_EC_PRIVINSN        7
#define          ESR_EC_STACKPROT       7  /* Same as PRIVINSN.  */
#define          ESR_EC_DATA_STORAGE    8
#define          ESR_EC_INSN_STORAGE    9
#define          ESR_EC_DATA_TLB        10
#define          ESR_EC_INSN_TLB        11
#define          ESR_EC_MASK            31

/* Floating Point Status Register (FSR) Bits */
#define FSR_IO          (1<<4) /* Invalid operation */
#define FSR_DZ          (1<<3) /* Divide-by-zero */
#define FSR_OF          (1<<2) /* Overflow */
#define FSR_UF          (1<<1) /* Underflow */
#define FSR_DO          (1<<0) /* Denormalized operand error */

/* Version reg.  */
/* Basic PVR mask */
#define PVR0_PVR_FULL_MASK              0x80000000
#define PVR0_USE_BARREL_MASK            0x40000000
#define PVR0_USE_DIV_MASK               0x20000000
#define PVR0_USE_HW_MUL_MASK            0x10000000
#define PVR0_USE_FPU_MASK               0x08000000
#define PVR0_USE_EXC_MASK               0x04000000
#define PVR0_USE_ICACHE_MASK            0x02000000
#define PVR0_USE_DCACHE_MASK            0x01000000
#define PVR0_USE_MMU_MASK               0x00800000
#define PVR0_USE_BTC			0x00400000
#define PVR0_ENDI_MASK                  0x00200000
#define PVR0_FAULT			0x00100000
#define PVR0_VERSION_MASK               0x0000FF00
#define PVR0_USER1_MASK                 0x000000FF
#define PVR0_SPROT_MASK                 0x00000001

/* User 2 PVR mask */
#define PVR1_USER2_MASK                 0xFFFFFFFF

/* Configuration PVR masks */
#define PVR2_D_OPB_MASK                 0x80000000
#define PVR2_D_LMB_MASK                 0x40000000
#define PVR2_I_OPB_MASK                 0x20000000
#define PVR2_I_LMB_MASK                 0x10000000
#define PVR2_INTERRUPT_IS_EDGE_MASK     0x08000000
#define PVR2_EDGE_IS_POSITIVE_MASK      0x04000000
#define PVR2_D_PLB_MASK                 0x02000000      /* new */
#define PVR2_I_PLB_MASK                 0x01000000      /* new */
#define PVR2_INTERCONNECT               0x00800000      /* new */
#define PVR2_USE_EXTEND_FSL             0x00080000      /* new */
#define PVR2_USE_FSL_EXC                0x00040000      /* new */
#define PVR2_USE_MSR_INSTR              0x00020000
#define PVR2_USE_PCMP_INSTR             0x00010000
#define PVR2_AREA_OPTIMISED             0x00008000
#define PVR2_USE_BARREL_MASK            0x00004000
#define PVR2_USE_DIV_MASK               0x00002000
#define PVR2_USE_HW_MUL_MASK            0x00001000
#define PVR2_USE_FPU_MASK               0x00000800
#define PVR2_USE_MUL64_MASK             0x00000400
#define PVR2_USE_FPU2_MASK              0x00000200      /* new */
#define PVR2_USE_IPLBEXC                0x00000100
#define PVR2_USE_DPLBEXC                0x00000080
#define PVR2_OPCODE_0x0_ILL_MASK        0x00000040
#define PVR2_UNALIGNED_EXC_MASK         0x00000020
#define PVR2_ILL_OPCODE_EXC_MASK        0x00000010
#define PVR2_IOPB_BUS_EXC_MASK          0x00000008
#define PVR2_DOPB_BUS_EXC_MASK          0x00000004
#define PVR2_DIV_ZERO_EXC_MASK          0x00000002
#define PVR2_FPU_EXC_MASK               0x00000001

/* Debug and exception PVR masks */
#define PVR3_DEBUG_ENABLED_MASK         0x80000000
#define PVR3_NUMBER_OF_PC_BRK_MASK      0x1E000000
#define PVR3_NUMBER_OF_RD_ADDR_BRK_MASK 0x00380000
#define PVR3_NUMBER_OF_WR_ADDR_BRK_MASK 0x0000E000
#define PVR3_FSL_LINKS_MASK             0x00000380

/* ICache config PVR masks */
#define PVR4_USE_ICACHE_MASK            0x80000000
#define PVR4_ICACHE_ADDR_TAG_BITS_MASK  0x7C000000
#define PVR4_ICACHE_USE_FSL_MASK        0x02000000
#define PVR4_ICACHE_ALLOW_WR_MASK       0x01000000
#define PVR4_ICACHE_LINE_LEN_MASK       0x00E00000
#define PVR4_ICACHE_BYTE_SIZE_MASK      0x001F0000

/* DCache config PVR masks */
#define PVR5_USE_DCACHE_MASK            0x80000000
#define PVR5_DCACHE_ADDR_TAG_BITS_MASK  0x7C000000
#define PVR5_DCACHE_USE_FSL_MASK        0x02000000
#define PVR5_DCACHE_ALLOW_WR_MASK       0x01000000
#define PVR5_DCACHE_LINE_LEN_MASK       0x00E00000
#define PVR5_DCACHE_BYTE_SIZE_MASK      0x001F0000
#define PVR5_DCACHE_WRITEBACK_MASK      0x00004000

/* ICache base address PVR mask */
#define PVR6_ICACHE_BASEADDR_MASK       0xFFFFFFFF

/* ICache high address PVR mask */
#define PVR7_ICACHE_HIGHADDR_MASK       0xFFFFFFFF

/* DCache base address PVR mask */
#define PVR8_DCACHE_BASEADDR_MASK       0xFFFFFFFF

/* DCache high address PVR mask */
#define PVR9_DCACHE_HIGHADDR_MASK       0xFFFFFFFF

/* Target family PVR mask */
#define PVR10_TARGET_FAMILY_MASK        0xFF000000

/* MMU descrtiption */
#define PVR11_USE_MMU                   0xC0000000
#define PVR11_MMU_ITLB_SIZE             0x38000000
#define PVR11_MMU_DTLB_SIZE             0x07000000
#define PVR11_MMU_TLB_ACCESS            0x00C00000
#define PVR11_MMU_ZONES                 0x003E0000
/* MSR Reset value PVR mask */
#define PVR11_MSR_RESET_VALUE_MASK      0x000007FF

#define C_PVR_NONE                      0
#define C_PVR_BASIC                     1
#define C_PVR_FULL                      2

/* CPU flags.  */

/* Condition codes.  */
#define CC_GE  5
#define CC_GT  4
#define CC_LE  3
#define CC_LT  2
#define CC_NE  1
#define CC_EQ  0

#define NB_MMU_MODES    3

#define STREAM_EXCEPTION (1 << 0)
#define STREAM_ATOMIC    (1 << 1)
#define STREAM_TEST      (1 << 2)
#define STREAM_CONTROL   (1 << 3)
#define STREAM_NONBLOCK  (1 << 4)

struct CPUMBState {
    uint32_t debug;
    uint32_t btaken;
    uint32_t btarget;
    uint32_t bimm;

    uint32_t imm;
    uint32_t regs[33];
    uint32_t sregs[24];
    float_status fp_status;
    /* Stack protectors. Yes, it's a hw feature.  */
    uint32_t slr, shr;

    /* lwx/swx reserved address */
#define RES_ADDR_NONE 0xffffffff /* Use 0xffffffff to indicate no reservation */
    uint32_t res_addr;
    uint32_t res_val;

    /* Internal flags.  */
#define IMM_FLAG	4
#define MSR_EE_FLAG     (1 << 8)
#define DRTI_FLAG	(1 << 16)
#define DRTE_FLAG	(1 << 17)
#define DRTB_FLAG	(1 << 18)
#define D_FLAG		(1 << 19)  /* Bit in ESR.  */
/* TB dependent CPUMBState.  */
#define IFLAGS_TB_MASK  (D_FLAG | IMM_FLAG | DRTI_FLAG | DRTE_FLAG | DRTB_FLAG)
    uint32_t iflags;

#if !defined(CONFIG_USER_ONLY)
    /* Unified MMU.  */
    struct microblaze_mmu mmu;
#endif

    CPU_COMMON

    /* These fields are preserved on reset.  */

    struct {
        uint32_t regs[16];
    } pvr;
};

/**
 * MicroBlazeCPU:
 * @env: #CPUMBState
 *
 * A MicroBlaze CPU.
 */
struct MicroBlazeCPU {
    /*< private >*/
    CPUState parent_obj;

    /*< public >*/

    /* Microblaze Configuration Settings */
    struct {
        bool stackprot;
        uint32_t base_vectors;
        uint8_t use_fpu;
        bool use_mmu;
        bool dcache_writeback;
        bool endi;
        char *version;
        uint8_t pvr;
    } cfg;

    CPUMBState env;
};

static inline MicroBlazeCPU *mb_env_get_cpu(CPUMBState *env)
{
    return container_of(env, MicroBlazeCPU, env);
}

#define ENV_GET_CPU(e) CPU(mb_env_get_cpu(e))

#define ENV_OFFSET offsetof(MicroBlazeCPU, env)

void mb_cpu_do_interrupt(CPUState *cs);
bool mb_cpu_exec_interrupt(CPUState *cs, int int_req);
void mb_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                       int flags);
hwaddr mb_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int mb_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int mb_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void mb_tcg_init(void);
MicroBlazeCPU *cpu_mb_init(const char *cpu_model);
int cpu_mb_exec(CPUState *cpu);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_mb_signal_handler(int host_signum, void *pinfo,
                          void *puc);

/* FIXME: MB uses variable pages down to 1K but linux only uses 4k.  */
#define TARGET_PAGE_BITS 12

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define cpu_init(cpu_model) CPU(cpu_mb_init(cpu_model))

#define cpu_exec cpu_mb_exec
#define cpu_signal_handler cpu_mb_signal_handler

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _nommu
#define MMU_MODE1_SUFFIX _kernel
#define MMU_MODE2_SUFFIX _user
#define MMU_NOMMU_IDX   0
#define MMU_KERNEL_IDX  1
#define MMU_USER_IDX    2
/* See NB_MMU_MODES further up the file.  */

static inline int cpu_mmu_index (CPUMBState *env, bool ifetch)
{
        /* Are we in nommu mode?.  */
        if (!(env->sregs[SR_MSR] & MSR_VM))
            return MMU_NOMMU_IDX;

	if (env->sregs[SR_MSR] & MSR_UM)
            return MMU_USER_IDX;
        return MMU_KERNEL_IDX;
}

int mb_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw,
                            int mmu_idx);

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUMBState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->sregs[SR_PC];
    *cs_base = 0;
    *flags = (env->iflags & IFLAGS_TB_MASK) |
                 (env->sregs[SR_MSR] & (MSR_UM | MSR_VM | MSR_EE));
}

#if !defined(CONFIG_USER_ONLY)
void mb_cpu_unassigned_access(CPUState *cpu, hwaddr addr,
                              bool is_write, bool is_exec, int is_asi,
                              unsigned size);
#endif

#endif
