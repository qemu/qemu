/*
   SPARC translation

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
   SPARC has two pitfalls: Delay slots and (a)nullification.
   This is currently solved as follows:

   'call' instructions simply execute the delay slot before the actual
   control transfer instructions.

   'jmpl' instructions execute calculate the destination, then execute
   the delay slot and then do the control transfer.

   (conditional) branch instructions are the most difficult ones, as the
   delay slot may be nullified (ie. not executed). This happens when a
   conditional branch is not executed (thus no control transfer happens)
   and the 'anull' bit in the branch instruction opcode is set. This is
   currently solved by doing a jump after the delay slot instruction.

   There is also one big (currently unsolved) bug in the branch code:
   If a delay slot modifies the condition codes then the new condition
   codes, instead of the old ones will be used.

   TODO-list:

   FPU-Instructions
   Coprocessor-Instructions
   Fix above bug
   Check signedness issues
   Privileged instructions
   Register window overflow/underflow check
   Optimize synthetic instructions
   Optional alignment and privileged instruction check

   -- TMO, 09/03/03
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#define DEBUG_DISAS

typedef struct DisasContext {
	uint8_t *pc;
	uint8_t *npc;
	void (*branch) (struct DisasContext *, uint32_t, uint32_t);
	unsigned int delay_slot:2;
	uint32_t insn;
	uint32_t target;
	int      is_br;
	struct TranslationBlock *tb;
} DisasContext;

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s,n,copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
	NB_OPS
};

#include "gen-op.h"

#define GET_FIELD(X, FROM, TO) \
  ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

#define IS_IMM (insn & (1<<13))

static void disas_sparc_insn (DisasContext *dc);

typedef void (GenOpFunc)(void);
typedef void (GenOpFunc1)(long);
typedef void (GenOpFunc2)(long, long);
typedef void (GenOpFunc3)(long, long, long);

static GenOpFunc *gen_op_movl_TN_reg[2][32] = {
	{
		gen_op_movl_g0_T0,
		gen_op_movl_g1_T0,
		gen_op_movl_g2_T0,
		gen_op_movl_g3_T0,
		gen_op_movl_g4_T0,
		gen_op_movl_g5_T0,
		gen_op_movl_g6_T0,
		gen_op_movl_g7_T0,
		gen_op_movl_o0_T0,
		gen_op_movl_o1_T0,
		gen_op_movl_o2_T0,
		gen_op_movl_o3_T0,
		gen_op_movl_o4_T0,
		gen_op_movl_o5_T0,
		gen_op_movl_o6_T0,
		gen_op_movl_o7_T0,
		gen_op_movl_l0_T0,
		gen_op_movl_l1_T0,
		gen_op_movl_l2_T0,
		gen_op_movl_l3_T0,
		gen_op_movl_l4_T0,
		gen_op_movl_l5_T0,
		gen_op_movl_l6_T0,
		gen_op_movl_l7_T0,
		gen_op_movl_i0_T0,
		gen_op_movl_i1_T0,
		gen_op_movl_i2_T0,
		gen_op_movl_i3_T0,
		gen_op_movl_i4_T0,
		gen_op_movl_i5_T0,
		gen_op_movl_i6_T0,
		gen_op_movl_i7_T0,
	},
	{
		gen_op_movl_g0_T1,
		gen_op_movl_g1_T1,
		gen_op_movl_g2_T1,
		gen_op_movl_g3_T1,
		gen_op_movl_g4_T1,
		gen_op_movl_g5_T1,
		gen_op_movl_g6_T1,
		gen_op_movl_g7_T1,
		gen_op_movl_o0_T1,
		gen_op_movl_o1_T1,
		gen_op_movl_o2_T1,
		gen_op_movl_o3_T1,
		gen_op_movl_o4_T1,
		gen_op_movl_o5_T1,
		gen_op_movl_o6_T1,
		gen_op_movl_o7_T1,
		gen_op_movl_l0_T1,
		gen_op_movl_l1_T1,
		gen_op_movl_l2_T1,
		gen_op_movl_l3_T1,
		gen_op_movl_l4_T1,
		gen_op_movl_l5_T1,
		gen_op_movl_l6_T1,
		gen_op_movl_l7_T1,
		gen_op_movl_i0_T1,
		gen_op_movl_i1_T1,
		gen_op_movl_i2_T1,
		gen_op_movl_i3_T1,
		gen_op_movl_i4_T1,
		gen_op_movl_i5_T1,
		gen_op_movl_i6_T1,
		gen_op_movl_i7_T1,
	}
};

static GenOpFunc *gen_op_movl_reg_TN[3][32] = {
	{
		gen_op_movl_T0_g0,
		gen_op_movl_T0_g1,
		gen_op_movl_T0_g2,
		gen_op_movl_T0_g3,
		gen_op_movl_T0_g4,
		gen_op_movl_T0_g5,
		gen_op_movl_T0_g6,
		gen_op_movl_T0_g7,
		gen_op_movl_T0_o0,
		gen_op_movl_T0_o1,
		gen_op_movl_T0_o2,
		gen_op_movl_T0_o3,
		gen_op_movl_T0_o4,
		gen_op_movl_T0_o5,
		gen_op_movl_T0_o6,
		gen_op_movl_T0_o7,
		gen_op_movl_T0_l0,
		gen_op_movl_T0_l1,
		gen_op_movl_T0_l2,
		gen_op_movl_T0_l3,
		gen_op_movl_T0_l4,
		gen_op_movl_T0_l5,
		gen_op_movl_T0_l6,
		gen_op_movl_T0_l7,
		gen_op_movl_T0_i0,
		gen_op_movl_T0_i1,
		gen_op_movl_T0_i2,
		gen_op_movl_T0_i3,
		gen_op_movl_T0_i4,
		gen_op_movl_T0_i5,
		gen_op_movl_T0_i6,
		gen_op_movl_T0_i7,
	},
	{
		gen_op_movl_T1_g0,
		gen_op_movl_T1_g1,
		gen_op_movl_T1_g2,
		gen_op_movl_T1_g3,
		gen_op_movl_T1_g4,
		gen_op_movl_T1_g5,
		gen_op_movl_T1_g6,
		gen_op_movl_T1_g7,
		gen_op_movl_T1_o0,
		gen_op_movl_T1_o1,
		gen_op_movl_T1_o2,
		gen_op_movl_T1_o3,
		gen_op_movl_T1_o4,
		gen_op_movl_T1_o5,
		gen_op_movl_T1_o6,
		gen_op_movl_T1_o7,
		gen_op_movl_T1_l0,
		gen_op_movl_T1_l1,
		gen_op_movl_T1_l2,
		gen_op_movl_T1_l3,
		gen_op_movl_T1_l4,
		gen_op_movl_T1_l5,
		gen_op_movl_T1_l6,
		gen_op_movl_T1_l7,
		gen_op_movl_T1_i0,
		gen_op_movl_T1_i1,
		gen_op_movl_T1_i2,
		gen_op_movl_T1_i3,
		gen_op_movl_T1_i4,
		gen_op_movl_T1_i5,
		gen_op_movl_T1_i6,
		gen_op_movl_T1_i7,
	},
	{
		gen_op_movl_T2_g0,
		gen_op_movl_T2_g1,
		gen_op_movl_T2_g2,
		gen_op_movl_T2_g3,
		gen_op_movl_T2_g4,
		gen_op_movl_T2_g5,
		gen_op_movl_T2_g6,
		gen_op_movl_T2_g7,
		gen_op_movl_T2_o0,
		gen_op_movl_T2_o1,
		gen_op_movl_T2_o2,
		gen_op_movl_T2_o3,
		gen_op_movl_T2_o4,
		gen_op_movl_T2_o5,
		gen_op_movl_T2_o6,
		gen_op_movl_T2_o7,
		gen_op_movl_T2_l0,
		gen_op_movl_T2_l1,
		gen_op_movl_T2_l2,
		gen_op_movl_T2_l3,
		gen_op_movl_T2_l4,
		gen_op_movl_T2_l5,
		gen_op_movl_T2_l6,
		gen_op_movl_T2_l7,
		gen_op_movl_T2_i0,
		gen_op_movl_T2_i1,
		gen_op_movl_T2_i2,
		gen_op_movl_T2_i3,
		gen_op_movl_T2_i4,
		gen_op_movl_T2_i5,
		gen_op_movl_T2_i6,
		gen_op_movl_T2_i7,
	}
};

static GenOpFunc1 *gen_op_movl_TN_im[3] = {
	gen_op_movl_T0_im,
	gen_op_movl_T1_im,
	gen_op_movl_T2_im
};

static inline void gen_movl_imm_TN (int reg, int imm)
{
	gen_op_movl_TN_im[reg](imm);
}

static inline void gen_movl_imm_T1 (int val)
{
	gen_movl_imm_TN (1, val);
}

static inline void gen_movl_imm_T0 (int val)
{
	gen_movl_imm_TN (0, val);
}

static inline void gen_movl_reg_TN (int reg, int t)
{
	if (reg) gen_op_movl_reg_TN[t][reg]();
	else gen_movl_imm_TN (t, 0);
}

static inline void gen_movl_reg_T0 (int reg)
{
	gen_movl_reg_TN (reg, 0);
}

static inline void gen_movl_reg_T1 (int reg)
{
	gen_movl_reg_TN (reg, 1);
}

static inline void gen_movl_reg_T2 (int reg)
{
	gen_movl_reg_TN (reg, 2);
}

static inline void gen_movl_TN_reg (int reg, int t)
{
	if (reg) gen_op_movl_TN_reg[t][reg]();
}

static inline void gen_movl_T0_reg (int reg)
{
	gen_movl_TN_reg (reg, 0);
}

static inline void gen_movl_T1_reg (int reg)
{
	gen_movl_TN_reg (reg, 1);
}

static void do_branch (DisasContext *dc, uint32_t target, uint32_t insn)
{
	unsigned int cond = GET_FIELD (insn, 3, 6), a = (insn & (1<<29)), ib = 0;
	target += (uint32_t) dc->pc-4;
	if (!a) disas_sparc_insn (dc);
	switch (cond) {
	  case 0x0: gen_op_movl_T0_0 (); break;
	  case 0x1: gen_op_eval_be (); break;
	  case 0x2: gen_op_eval_ble (); break;
	  case 0x3: gen_op_eval_bl (); break;
	  case 0x4: gen_op_eval_bleu (); break;
	  case 0x5: gen_op_eval_bcs (); break;
	  case 0x6: gen_op_eval_bneg (); break;
	  case 0x7: gen_op_eval_bvs (); break;
	  case 0x8: gen_op_movl_T0_1 (); break;
	  case 0x9: gen_op_eval_bne (); break;
	  case 0xa: gen_op_eval_bg (); break;
	  case 0xb: gen_op_eval_bge (); break;
	  case 0xc: gen_op_eval_bgu (); break;
	  case 0xd: gen_op_eval_bcc (); break;
	  case 0xe: gen_op_eval_bpos (); break;
	  case 0xf: gen_op_eval_bvc (); break;
	}
	if (a && ((cond|0x8) != 0x8)) {
		gen_op_generic_branch_a ((uint32_t) dc->tb,
				(uint32_t) dc->pc+4, target);
		disas_sparc_insn (dc);
		ib = 1;
	}
	else
	if (cond && !a) {
		gen_op_generic_branch ((uint32_t) dc->tb, (uint32_t) target,
			(uint32_t) dc->pc);
		ib = 1;
	}
	if (ib) dc->is_br = DISAS_JUMP;
}

/* target == 0x1 means CALL- else JMPL-instruction */
static void do_jump (DisasContext *dc, uint32_t target, uint32_t rd)
{
	uint32_t orig_pc = (uint32_t) dc->pc-8;
	if (target != 0x1)
	 gen_op_generic_jmp_1 (orig_pc, target);
	else
	 gen_op_generic_jmp_2 (orig_pc);
	gen_movl_T1_reg (rd);
	dc->is_br = DISAS_JUMP;
	gen_op_movl_T0_0 ();
}

#define GET_FIELDs(x,a,b) sign_extend (GET_FIELD(x,a,b), b-a)

static int
sign_extend (x, len)
	int x, len;
{
	int signbit = (1 << (len - 1));
	int mask = (signbit << 1) - 1;
	return ((x & mask) ^ signbit) - signbit;
}

static void disas_sparc_insn (DisasContext *dc)
{
	unsigned int insn, opc, rs1, rs2, rd;

	if (dc->delay_slot == 1) {
		insn = dc->insn;
	} else {
		if (dc->delay_slot) dc->delay_slot--;
		insn = htonl (*(unsigned int *) (dc->pc));
		dc->pc += 4;
	}

	opc = GET_FIELD (insn, 0, 1);

	rd  = GET_FIELD (insn, 2, 6);
	switch (opc) {
	  case 0: /* branches/sethi */
		{
			unsigned int xop = GET_FIELD (insn, 7, 9);
			int target;
			target = GET_FIELD (insn, 10, 31);
			switch (xop) {
			 case 0x0: case 0x1: /* UNIMPL */
				printf ("UNIMPLEMENTED: %p\n", dc->pc-4);
				exit (23);
				break;
			 case 0x2: /* BN+x */
			{
				target <<= 2;
				target = sign_extend (target, 22);
				do_branch (dc, target, insn);
				break;
			}
			 case 0x3: /* FBN+x */
				break;
			 case 0x4: /* SETHI */
				gen_movl_imm_T0 (target<<10);
				gen_movl_T0_reg (rd);
				break;
			 case 0x5: /*CBN+x*/
				break;
			}
			break;
		}
	  case 1: /*CALL*/
		{
			unsigned int target = GET_FIELDs (insn, 2, 31) << 2;
			if (dc->delay_slot) {
				do_jump (dc, target, 15);
				dc->delay_slot = 0;
			} else {
				dc->insn = insn;
				dc->delay_slot = 2;
			}
			break;
		}
	  case 2: /* FPU & Logical Operations */
		{
			unsigned int xop = GET_FIELD (insn, 7, 12);
			if (xop == 58) { /* generate trap */
				dc->is_br = DISAS_JUMP;
				gen_op_jmp_im ((uint32_t) dc->pc);
				if (IS_IMM) gen_op_trap (GET_FIELD (insn, 25, 31));
				/* else XXX*/
				gen_op_movl_T0_0 ();
				break;
			}
			if (xop == 0x34 || xop == 0x35) { /* FPU Operations */
				exit (33);
			}
			rs1 = GET_FIELD (insn, 13, 17);
			gen_movl_reg_T0 (rs1);
			if (IS_IMM) { /* immediate */
				rs2 = GET_FIELDs (insn, 20, 31);
				gen_movl_imm_T1 (rs2);
			} else {              /* register */
				rs2 = GET_FIELD (insn, 27, 31);
				gen_movl_reg_T1 (rs2);
			}
			if (xop < 0x20) {
			 switch (xop &~ 0x10) {
			  case 0x0:
				gen_op_add_T1_T0 ();
				break;
			  case 0x1:
				gen_op_and_T1_T0 ();
				break;
			  case 0x2:
				gen_op_or_T1_T0 ();
				break;
			  case 0x3:
				gen_op_xor_T1_T0 ();
				break;
			  case 0x4:
				gen_op_sub_T1_T0 ();
				break;
			  case 0x5:
				gen_op_andn_T1_T0 ();
				break;
			  case 0x6:
				gen_op_orn_T1_T0 ();
				break;
			  case 0x7:
				gen_op_xnor_T1_T0 ();
				break;
			  case 0x8:
				gen_op_addx_T1_T0 ();
				break;
			  case 0xa:
				gen_op_umul_T1_T0 ();
				break;
			  case 0xb:
				gen_op_smul_T1_T0 ();
				break;
			  case 0xc:
				gen_op_subx_T1_T0 ();
				break;
			  case 0xe:
				gen_op_udiv_T1_T0 ();
				break;
			  case 0xf:
				gen_op_sdiv_T1_T0 ();
				break;
			  default:
				exit (17);
				break;
			 }
			 gen_movl_T0_reg (rd);
			 if (xop & 0x10) {
				gen_op_set_flags ();
			 }
			} else {
			  switch (xop) {
				case 0x25: /* SLL */
					gen_op_sll ();
					break;
				case 0x26:
					gen_op_srl ();
					break;
				case 0x27:
					gen_op_sra ();
					break;
				case 0x28: case 0x30:
				{
					unsigned int rdi = GET_FIELD (insn, 13, 17);
					if (!rdi) (xop==0x28?gen_op_rdy ():gen_op_wry());
					/* else gen_op_su_trap (); */
					break;
				}
				/* Problem with jmpl: if restore is executed in the delay
				   slot, then the wrong registers are beeing used */
				case 0x38: /* jmpl */
				{
					if (dc->delay_slot) {
						gen_op_add_T1_T0 ();
						do_jump (dc, 1, rd);
						dc->delay_slot = 0;
					} else {
						gen_op_add_T1_T0 ();
						gen_op_jmpl ();
						dc->insn = insn;
						dc->delay_slot = 2;
					}
					break;
				}
				case 0x3c: /* save */
					gen_op_add_T1_T0 ();
					gen_op_save ();
					gen_movl_T0_reg (rd);
					break;
				case 0x3d: /* restore */
					gen_op_add_T1_T0 ();
					gen_op_restore ();
					gen_movl_T0_reg (rd);
					break;
			  }
			}
			break;
		}
	  case 3: /* load/store instructions */
		{
			unsigned int xop = GET_FIELD (insn, 7, 12);
			rs1 = GET_FIELD (insn, 13, 17);
			gen_movl_reg_T0 (rs1);
			if (IS_IMM) { /* immediate */
				rs2 = GET_FIELDs (insn, 20, 31);
				gen_movl_imm_T1 (rs2);
			} else {              /* register */
				rs2 = GET_FIELD (insn, 27, 31);
				gen_movl_reg_T1 (rs2);
			}
			gen_op_add_T1_T0 ();
			if (xop < 4 || xop > 7)  {
			 switch (xop) {
			  case 0x0: /* load word */
				gen_op_ld ();
				break;
			  case 0x1: /* load unsigned byte */
				gen_op_ldub ();
				break;
			  case 0x2: /* load unsigned halfword */
				gen_op_lduh ();
				break;
			  case 0x3: /* load double word */
				gen_op_ldd ();
				gen_movl_T0_reg (rd+1);
				break;
			  case 0x9: /* load signed byte */
				gen_op_ldsb ();
				break;
			  case 0xa: /* load signed halfword */
				gen_op_ldsh ();
				break;
			  case 0xd: /* ldstub -- XXX: should be atomically */
				gen_op_ldstub ();
				break;
			  case 0x0f: /* swap register with memory. Also atomically */
				gen_op_swap ();
				break;
			 }
			 gen_movl_T1_reg (rd);
			} else if (xop < 8) {
			 gen_movl_reg_T1 (rd);
			 switch (xop) {
			  case 0x4:
				gen_op_st ();
				break;
			  case 0x5:
				gen_op_stb ();
				break;
			  case 0x6:
				gen_op_sth ();
				break;
			  case 0x7:
				gen_op_st ();
				gen_movl_reg_T1 (rd+1);
				gen_op_st ();
				break;
			 }
			}
		}
	}
}

static inline int gen_intermediate_code_internal (TranslationBlock *tb, int spc)
{
	uint8_t *pc_start = (uint8_t *) tb->pc;
	uint16_t *gen_opc_end;
	DisasContext dc;

	memset (&dc, 0, sizeof (dc));
	if (spc) {
		printf ("SearchPC not yet supported\n");
		exit (0);
	}
	dc.tb = tb;
	dc.pc = pc_start;

	gen_opc_ptr = gen_opc_buf;
	gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
	gen_opparam_ptr = gen_opparam_buf;

	do {
		disas_sparc_insn (&dc);
	} while (!dc.is_br && (gen_opc_ptr < gen_opc_end) &&
		(dc.pc - pc_start) < (TARGET_PAGE_SIZE - 32));

	switch (dc.is_br) {
	  case DISAS_JUMP:
	  case DISAS_TB_JUMP:
		gen_op_exit_tb ();
		break;
	}

	*gen_opc_ptr = INDEX_op_end;
#ifdef DEBUG_DISAS
	if (loglevel) {
		fprintf (logfile, "--------------\n");
		fprintf (logfile, "IN: %s\n", lookup_symbol (pc_start));
		disas(logfile, pc_start, dc.pc - pc_start, 0, 0);
		fprintf(logfile, "\n");
		fprintf(logfile, "OP:\n");
		dump_ops(gen_opc_buf, gen_opparam_buf);
		fprintf(logfile, "\n");
	}
#endif

	return 0;
}

int gen_intermediate_code (CPUSPARCState *env, TranslationBlock *tb)
{
	return gen_intermediate_code_internal(tb, 0);
}

int gen_intermediate_code_pc (CPUSPARCState *env, TranslationBlock *tb)
{
	return gen_intermediate_code_internal(tb, 1);
}

void *mycpu;

CPUSPARCState *cpu_sparc_init (void)
{
	CPUSPARCState *env;

	cpu_exec_init ();

	if (!(env = malloc (sizeof(CPUSPARCState))))
		return (NULL);
	memset (env, 0, sizeof (*env));
	if (!(env->regwptr = malloc (0x2000)))
		return (NULL);
	memset (env->regwptr, 0, 0x2000);
	env->regwptr += 127;
	env->user_mode_only = 1;
	mycpu = env;
	return (env);
}

#define GET_FLAG(a,b) ((env->psr & a)?b:'-')

void cpu_sparc_dump_state (CPUSPARCState *env, FILE *f, int flags)
{
	int i, x;

	fprintf (f, "@PC: %p\n", (void *) env->pc);
	fprintf (f, "General Registers:\n");
	for (i=0;i<4;i++)
		fprintf (f, "%%g%c: %%%08x\t", i+'0', env->gregs[i]);
	fprintf (f, "\n");
	for (;i<8;i++)
		fprintf (f, "%%g%c: %%%08x\t", i+'0', env->gregs[i]);
	fprintf (f, "\nCurrent Register Window:\n");
	for (x=0;x<3;x++) {
	  for (i=0;i<4;i++)
		fprintf (f, "%%%c%d: %%%08x\t", (x==0?'o':(x==1?'l':'i')), i, env->regwptr[i+x*8]);
	  fprintf (f, "\n");
	  for (;i<8;i++)
		fprintf (f, "%%%c%d: %%%08x\t", (x==0?'o':x==1?'l':'i'), i, env->regwptr[i+x*8]);
	  fprintf (f, "\n");
	}
	fprintf (f, "PSR: %x -> %c%c%c%c\n", env->psr,
			GET_FLAG(PSR_ZERO, 'Z'), GET_FLAG(PSR_OVF, 'V'),
			GET_FLAG(PSR_NEG, 'N'),  GET_FLAG(PSR_CARRY, 'C'));
}
