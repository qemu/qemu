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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_CRIS_H
#define CPU_CRIS_H

#define TARGET_LONG_BITS 32

#include "cpu-defs.h"

#include "softfloat.h"

#define TARGET_HAS_ICE 1

#define ELF_MACHINE	EM_CRIS

#define EXCP_MMU_EXEC    0
#define EXCP_MMU_READ    1
#define EXCP_MMU_WRITE   2
#define EXCP_MMU_FLUSH   3
#define EXCP_MMU_MISS    4
#define EXCP_BREAK      16 /* trap.  */

/* CPU flags.  */
#define S_FLAG 0x200
#define R_FLAG 0x100
#define P_FLAG 0x80
#define U_FLAG 0x40
#define P_FLAG 0x80
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

/* Internal flags for the implementation.  */
#define F_DELAYSLOT 1

#define NB_MMU_MODES 2

typedef struct CPUCRISState {
	uint32_t debug1;
	uint32_t debug2;
	uint32_t debug3;

	/*
	 * We just store the stores to the tlbset here for later evaluation
	 * when the hw needs access to them.
	 *
	 * One for I and another for D.
	 */
	struct
	{
		uint32_t hi;
		uint32_t lo;
	} tlbsets[2][4][16];

	uint32_t sregs[256][16]; /* grrr why so many??  */
	uint32_t regs[16];
	uint32_t pregs[16];
	uint32_t pc;
	uint32_t sr;
	uint32_t flag_mask; /* Per insn mask of affected flags.  */

	/* SSP and USP.  */
	int current_sp;
	uint32_t sp[2];

	/* These are setup up by the guest code just before transfering the
	   control back to the host.  */
	int jmp;
	uint32_t btarget;
	int btaken;

	/* for traps.  */
	int trapnr;

	/* Condition flag tracking.  */
	uint32_t cc_op;
	uint32_t cc_mask;
	uint32_t cc_dest;
	uint32_t cc_src;
	uint32_t cc_result;

	/* size of the operation, 1 = byte, 2 = word, 4 = dword.  */
	int cc_size;

	/* extended arithmetics.  */
	int cc_x_live;
	int cc_x;

	int features;

        uint64_t pending_interrupts;
	int interrupt_request;
	int exception_index;
	int user_mode_only;
	int halted;

	struct
	{
		int exec_insns;
		int exec_loads;
		int exec_stores;
	} stats;


	jmp_buf jmp_env;
	CPU_COMMON
} CPUCRISState;

CPUCRISState *cpu_cris_init(const char *cpu_model);
int cpu_cris_exec(CPUCRISState *s);
void cpu_cris_close(CPUCRISState *s);
void do_interrupt(CPUCRISState *env);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_cris_signal_handler(int host_signum, void *pinfo,
                           void *puc);
void cpu_cris_flush_flags(CPUCRISState *, int);


void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi);

enum {
    CC_OP_DYNAMIC, /* Use env->cc_op  */
    CC_OP_FLAGS,
    CC_OP_LOGIC,
    CC_OP_CMP,
    CC_OP_MOVE,
    CC_OP_MOVE_PD,
    CC_OP_MOVE_SD,
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
    CC_OP_BOUND,

    CC_OP_OR,
    CC_OP_AND,
    CC_OP_XOR,
    CC_OP_LSL,
    CC_OP_LSR,
    CC_OP_ASR,
    CC_OP_LZ
};

#define CCF_C 0x01
#define CCF_V 0x02
#define CCF_Z 0x04
#define CCF_N 0x08
#define CCF_X 0x10

#define CRIS_SSP    0
#define CRIS_USP    1

void cris_set_irq_level(CPUCRISState *env, int level, uint8_t vector);
void cris_set_macsr(CPUCRISState *env, uint32_t val);
void cris_switch_sp(CPUCRISState *env);

void do_cris_semihosting(CPUCRISState *env, int nr);

enum cris_features {
    CRIS_FEATURE_CF_ISA_MUL,
};

static inline int cris_feature(CPUCRISState *env, int feature)
{
    return (env->features & (1u << feature)) != 0;
}

void register_cris_insns (CPUCRISState *env);

/* CRIS uses 8k pages.  */
#define TARGET_PAGE_BITS 13

#define CPUState CPUCRISState
#define cpu_init cpu_cris_init
#define cpu_exec cpu_cris_exec
#define cpu_gen_code cpu_cris_gen_code
#define cpu_signal_handler cpu_cris_signal_handler

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
/* CRIS FIXME: I guess we want to validate supervisor mode acceses here.  */
static inline int cpu_mmu_index (CPUState *env)
{
    return 0;
}

#include "cpu-all.h"

/* Register aliases.  */
#define REG_SP  14
#define REG_ACR 15
#define REG_MOF 7

/* Support regs.  */
#define SR_PID 2
#define SR_SRS 3
#define SR_EBP 9
#define SR_ERP 10
#define SR_CCS 13

/* Support func regs.  */
#define SFR_RW_GC_CFG      0][0
#define SFR_RW_MM_CFG      1][0
#define SFR_RW_MM_KBASE_LO 1][1
#define SFR_RW_MM_KBASE_HI 1][2
#define SFR_R_MM_CAUSE     1][3
#define SFR_RW_MM_TLB_SEL  1][4
#define SFR_RW_MM_TLB_LO   1][5
#define SFR_RW_MM_TLB_HI   1][6

#endif
