/*
 * m68k virtual CPU header
 *
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Written by Paul Brook
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
#ifndef CPU_M68K_H
#define CPU_M68K_H

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUM68KState

#include "qemu-common.h"
#include "exec/cpu-defs.h"

#include "fpu/softfloat.h"

#define MAX_QREGS 32

#define EXCP_ACCESS         2   /* Access (MMU) error.  */
#define EXCP_ADDRESS        3   /* Address error.  */
#define EXCP_ILLEGAL        4   /* Illegal instruction.  */
#define EXCP_DIV0           5   /* Divide by zero */
#define EXCP_PRIVILEGE      8   /* Privilege violation.  */
#define EXCP_TRACE          9
#define EXCP_LINEA          10  /* Unimplemented line-A (MAC) opcode.  */
#define EXCP_LINEF          11  /* Unimplemented line-F (FPU) opcode.  */
#define EXCP_DEBUGNBP       12  /* Non-breakpoint debug interrupt.  */
#define EXCP_DEBEGBP        13  /* Breakpoint debug interrupt.  */
#define EXCP_FORMAT         14  /* RTE format error.  */
#define EXCP_UNINITIALIZED  15
#define EXCP_TRAP0          32   /* User trap #0.  */
#define EXCP_TRAP15         47   /* User trap #15.  */
#define EXCP_UNSUPPORTED    61
#define EXCP_ICE            13

#define EXCP_RTE            0x100
#define EXCP_HALT_INSN      0x101

#define NB_MMU_MODES 2

typedef struct CPUM68KState {
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint32_t pc;
    uint32_t sr;

    /* SSP and USP.  The current_sp is stored in aregs[7], the other here.  */
    int current_sp;
    uint32_t sp[2];

    /* Condition flags.  */
    uint32_t cc_op;
    uint32_t cc_dest;
    uint32_t cc_src;
    uint32_t cc_x;

    float64 fregs[8];
    float64 fp_result;
    uint32_t fpcr;
    uint32_t fpsr;
    float_status fp_status;

    uint64_t mactmp;
    /* EMAC Hardware deals with 48-bit values composed of one 32-bit and
       two 8-bit parts.  We store a single 64-bit value and
       rearrange/extend this when changing modes.  */
    uint64_t macc[4];
    uint32_t macsr;
    uint32_t mac_mask;

    /* Temporary storage for DIV helpers.  */
    uint32_t div1;
    uint32_t div2;

    /* MMU status.  */
    struct {
        uint32_t ar;
    } mmu;

    /* Control registers.  */
    uint32_t vbr;
    uint32_t mbar;
    uint32_t rambar0;
    uint32_t cacr;

    int pending_vector;
    int pending_level;

    uint32_t qregs[MAX_QREGS];

    CPU_COMMON

    /* Fields from here on are preserved across CPU reset. */
    uint32_t features;
} CPUM68KState;

#include "cpu-qom.h"

void m68k_tcg_init(void);
void m68k_cpu_init_gdb(M68kCPU *cpu);
M68kCPU *cpu_m68k_init(const char *cpu_model);
int cpu_m68k_exec(CPUState *cpu);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_m68k_signal_handler(int host_signum, void *pinfo,
                           void *puc);
void cpu_m68k_flush_flags(CPUM68KState *, int);

enum {
    CC_OP_DYNAMIC, /* Use env->cc_op  */
    CC_OP_FLAGS, /* CC_DEST = CVZN, CC_SRC = unused */
    CC_OP_LOGIC, /* CC_DEST = result, CC_SRC = unused */
    CC_OP_ADD,   /* CC_DEST = result, CC_SRC = source */
    CC_OP_SUB,   /* CC_DEST = result, CC_SRC = source */
    CC_OP_CMPB,  /* CC_DEST = result, CC_SRC = source */
    CC_OP_CMPW,  /* CC_DEST = result, CC_SRC = source */
    CC_OP_ADDX,  /* CC_DEST = result, CC_SRC = source */
    CC_OP_SUBX,  /* CC_DEST = result, CC_SRC = source */
    CC_OP_SHIFT, /* CC_DEST = result, CC_SRC = carry */
};

#define CCF_C 0x01
#define CCF_V 0x02
#define CCF_Z 0x04
#define CCF_N 0x08
#define CCF_X 0x10

#define SR_I_SHIFT 8
#define SR_I  0x0700
#define SR_M  0x1000
#define SR_S  0x2000
#define SR_T  0x8000

#define M68K_SSP    0
#define M68K_USP    1

/* CACR fields are implementation defined, but some bits are common.  */
#define M68K_CACR_EUSP  0x10

#define MACSR_PAV0  0x100
#define MACSR_OMC   0x080
#define MACSR_SU    0x040
#define MACSR_FI    0x020
#define MACSR_RT    0x010
#define MACSR_N     0x008
#define MACSR_Z     0x004
#define MACSR_V     0x002
#define MACSR_EV    0x001

void m68k_set_irq_level(M68kCPU *cpu, int level, uint8_t vector);
void m68k_set_macsr(CPUM68KState *env, uint32_t val);
void m68k_switch_sp(CPUM68KState *env);

#define M68K_FPCR_PREC (1 << 6)

void do_m68k_semihosting(CPUM68KState *env, int nr);

/* There are 4 ColdFire core ISA revisions: A, A+, B and C.
   Each feature covers the subset of instructions common to the
   ISA revisions mentioned.  */

enum m68k_features {
    M68K_FEATURE_CF_ISA_A,
    M68K_FEATURE_CF_ISA_B, /* (ISA B or C).  */
    M68K_FEATURE_CF_ISA_APLUSC, /* BIT/BITREV, FF1, STRLDSR (ISA A+ or C).  */
    M68K_FEATURE_BRAL, /* Long unconditional branch.  (ISA A+ or B).  */
    M68K_FEATURE_CF_FPU,
    M68K_FEATURE_CF_MAC,
    M68K_FEATURE_CF_EMAC,
    M68K_FEATURE_CF_EMAC_B, /* Revision B EMAC (dual accumulate).  */
    M68K_FEATURE_USP, /* User Stack Pointer.  (ISA A+, B or C).  */
    M68K_FEATURE_EXT_FULL, /* 68020+ full extension word.  */
    M68K_FEATURE_WORD_INDEX /* word sized address index registers.  */
};

static inline int m68k_feature(CPUM68KState *env, int feature)
{
    return (env->features & (1u << feature)) != 0;
}

void m68k_cpu_list(FILE *f, fprintf_function cpu_fprintf);

void register_m68k_insns (CPUM68KState *env);

#ifdef CONFIG_USER_ONLY
/* Linux uses 8k pages.  */
#define TARGET_PAGE_BITS 13
#else
/* Smallest TLB entry size is 1k.  */
#define TARGET_PAGE_BITS 10
#endif

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define cpu_init(cpu_model) CPU(cpu_m68k_init(cpu_model))

#define cpu_exec cpu_m68k_exec
#define cpu_signal_handler cpu_m68k_signal_handler
#define cpu_list m68k_cpu_list

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUM68KState *env, bool ifetch)
{
    return (env->sr & SR_S) == 0 ? 1 : 0;
}

int m68k_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw,
                              int mmu_idx);

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUM68KState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = (env->fpcr & M68K_FPCR_PREC)       /* Bit  6 */
            | (env->sr & SR_S)                  /* Bit  13 */
            | ((env->macsr >> 4) & 0xf);        /* Bits 0-3 */
}

#include "exec/exec-all.h"

#endif
