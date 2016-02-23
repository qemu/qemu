/*
 *  CRIS virtual CPU header
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias
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
#ifndef CPU_CRIS_H
#define CPU_CRIS_H

#include "qemu-common.h"

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUCRISState

#include "exec/cpu-defs.h"

#define EXCP_NMI        1
#define EXCP_GURU       2
#define EXCP_BUSFAULT   3
#define EXCP_IRQ        4
#define EXCP_BREAK      5

/* CRIS-specific interrupt pending bits.  */
#define CPU_INTERRUPT_NMI       CPU_INTERRUPT_TGT_EXT_3

/* CRUS CPU device objects interrupt lines.  */
#define CRIS_CPU_IRQ 0
#define CRIS_CPU_NMI 1

/* Register aliases. R0 - R15 */
#define R_FP  8
#define R_SP  14
#define R_ACR 15

/* Support regs, P0 - P15  */
#define PR_BZ  0
#define PR_VR  1
#define PR_PID 2
#define PR_SRS 3
#define PR_WZ  4
#define PR_EXS 5
#define PR_EDA 6
#define PR_PREFIX 6    /* On CRISv10 P6 is reserved, we use it as prefix.  */
#define PR_MOF 7
#define PR_DZ  8
#define PR_EBP 9
#define PR_ERP 10
#define PR_SRP 11
#define PR_NRP 12
#define PR_CCS 13
#define PR_USP 14
#define PRV10_BRP 14
#define PR_SPC 15

/* CPU flags.  */
#define Q_FLAG 0x80000000
#define M_FLAG_V32 0x40000000
#define PFIX_FLAG 0x800      /* CRISv10 Only.  */
#define F_FLAG_V10 0x400
#define P_FLAG_V10 0x200
#define S_FLAG 0x200
#define R_FLAG 0x100
#define P_FLAG 0x80
#define M_FLAG_V10 0x80
#define U_FLAG 0x40
#define I_FLAG 0x20
#define X_FLAG 0x10
#define N_FLAG 0x08
#define Z_FLAG 0x04
#define V_FLAG 0x02
#define C_FLAG 0x01
#define ALU_FLAGS 0x1F

/* Condition codes.  */
#define CC_CC   0
#define CC_CS   1
#define CC_NE   2
#define CC_EQ   3
#define CC_VC   4
#define CC_VS   5
#define CC_PL   6
#define CC_MI   7
#define CC_LS   8
#define CC_HI   9
#define CC_GE  10
#define CC_LT  11
#define CC_GT  12
#define CC_LE  13
#define CC_A   14
#define CC_P   15

#define NB_MMU_MODES 2

typedef struct {
    uint32_t hi;
    uint32_t lo;
} TLBSet;

typedef struct CPUCRISState {
	uint32_t regs[16];
	/* P0 - P15 are referred to as special registers in the docs.  */
	uint32_t pregs[16];

	/* Pseudo register for the PC. Not directly accessible on CRIS.  */
	uint32_t pc;

	/* Pseudo register for the kernel stack.  */
	uint32_t ksp;

	/* Branch.  */
	int dslot;
	int btaken;
	uint32_t btarget;

	/* Condition flag tracking.  */
	uint32_t cc_op;
	uint32_t cc_mask;
	uint32_t cc_dest;
	uint32_t cc_src;
	uint32_t cc_result;
	/* size of the operation, 1 = byte, 2 = word, 4 = dword.  */
	int cc_size;
	/* X flag at the time of cc snapshot.  */
	int cc_x;

	/* CRIS has certain insns that lockout interrupts.  */
	int locked_irq;
	int interrupt_vector;
	int fault_vector;
	int trap_vector;

	/* FIXME: add a check in the translator to avoid writing to support
	   register sets beyond the 4th. The ISA allows up to 256! but in
	   practice there is no core that implements more than 4.

	   Support function registers are used to control units close to the
	   core. Accesses do not pass down the normal hierarchy.
	*/
	uint32_t sregs[4][16];

	/* Linear feedback shift reg in the mmu. Used to provide pseudo
	   randomness for the 'hint' the mmu gives to sw for choosing valid
	   sets on TLB refills.  */
	uint32_t mmu_rand_lfsr;

	/*
	 * We just store the stores to the tlbset here for later evaluation
	 * when the hw needs access to them.
	 *
	 * One for I and another for D.
	 */
        TLBSet tlbsets[2][4][16];

	CPU_COMMON

    /* Members from load_info on are preserved across resets.  */
    void *load_info;
} CPUCRISState;

#include "cpu-qom.h"

CRISCPU *cpu_cris_init(const char *cpu_model);
int cpu_cris_exec(CPUState *cpu);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_cris_signal_handler(int host_signum, void *pinfo,
                           void *puc);

void cris_initialize_tcg(void);
void cris_initialize_crisv10_tcg(void);

enum {
    CC_OP_DYNAMIC, /* Use env->cc_op  */
    CC_OP_FLAGS,
    CC_OP_CMP,
    CC_OP_MOVE,
    CC_OP_ADD,
    CC_OP_ADDC,
    CC_OP_MCP,
    CC_OP_ADDU,
    CC_OP_SUB,
    CC_OP_SUBU,
    CC_OP_NEG,
    CC_OP_BTST,
    CC_OP_MULS,
    CC_OP_MULU,
    CC_OP_DSTEP,
    CC_OP_MSTEP,
    CC_OP_BOUND,

    CC_OP_OR,
    CC_OP_AND,
    CC_OP_XOR,
    CC_OP_LSL,
    CC_OP_LSR,
    CC_OP_ASR,
    CC_OP_LZ
};

/* CRIS uses 8k pages.  */
#define TARGET_PAGE_BITS 13
#define MMAP_SHIFT TARGET_PAGE_BITS

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define cpu_init(cpu_model) CPU(cpu_cris_init(cpu_model))

#define cpu_exec cpu_cris_exec
#define cpu_signal_handler cpu_cris_signal_handler

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUCRISState *env, bool ifetch)
{
	return !!(env->pregs[PR_CCS] & U_FLAG);
}

int cris_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw,
                              int mmu_idx);

/* Support function regs.  */
#define SFR_RW_GC_CFG      0][0
#define SFR_RW_MM_CFG      env->pregs[PR_SRS]][0
#define SFR_RW_MM_KBASE_LO env->pregs[PR_SRS]][1
#define SFR_RW_MM_KBASE_HI env->pregs[PR_SRS]][2
#define SFR_R_MM_CAUSE     env->pregs[PR_SRS]][3
#define SFR_RW_MM_TLB_SEL  env->pregs[PR_SRS]][4
#define SFR_RW_MM_TLB_LO   env->pregs[PR_SRS]][5
#define SFR_RW_MM_TLB_HI   env->pregs[PR_SRS]][6

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUCRISState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = env->dslot |
            (env->pregs[PR_CCS] & (S_FLAG | P_FLAG | U_FLAG
				     | X_FLAG | PFIX_FLAG));
}

#define cpu_list cris_cpu_list
void cris_cpu_list(FILE *f, fprintf_function cpu_fprintf);

#include "exec/exec-all.h"

#endif
