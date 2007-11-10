/*
 *  CRIS emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This file implements a CRIS decoder-stage in SW. The decoder translates the
 * guest (CRIS) machine-code into host machine code via dyngen using the
 * micro-operations described in op.c
 *
 * The micro-operations for CRIS translation implement a RISC style ISA.
 * Note that the micro-operations typically order their operands
 * starting with the dst. CRIS asm, does the opposite.
 *
 * For example the following CRIS code:
 * add.d [$r0], $r1
 *
 * translates into:
 *
 * gen_movl_T0_reg(0);   // Fetch $r0 into T0
 * gen_load_T0_T0();     // Load T0, @T0
 * gen_movl_reg_T0(1);   // Writeback T0 into $r1
 *
 * The actual names for the micro-code generators vary but the example
 * illustrates the point.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "crisv32-decode.h"

#define CRIS_STATS 0
#if CRIS_STATS
#define STATS(x) x
#else
#define STATS(x)
#endif

#define DISAS_CRIS 0
#if DISAS_CRIS
#define DIS(x) x
#else
#define DIS(x)
#endif

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

#define BUG() (gen_BUG(dc, __FILE__, __LINE__))
#define BUG_ON(x) ({if (x) BUG();})

/* Used by the decoder.  */
#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

#define CC_MASK_NZ 0xc
#define CC_MASK_NZV 0xe
#define CC_MASK_NZVC 0xf
#define CC_MASK_RNZV 0x10e

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};
#include "gen-op.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
	CPUState *env;
	target_ulong pc, insn_pc;

	/* Decoder.  */
	uint32_t ir;
	uint32_t opcode;
	unsigned int op1;
	unsigned int op2;
	unsigned int zsize, zzsize;
	unsigned int mode;
	unsigned int postinc;


	struct
	{
		int op;
		int size;
		unsigned int mask;
	} cc_state[3];
	int cc_i;

	int update_cc;
	int cc_op;
	int cc_size;
	uint32_t cc_mask;
	int flags_live;
	int flagx_live;
	int flags_x;
	uint32_t tb_entry_flags;

	int memidx; /* user or kernel mode.  */
	int is_jmp;
	int dyn_jmp;

	uint32_t delayed_pc;
	int delayed_branch;
	int bcc;
	uint32_t condlabel;

	struct TranslationBlock *tb;
	int singlestep_enabled;
} DisasContext;

void cris_prepare_jmp (DisasContext *dc, uint32_t dst);
static void gen_BUG(DisasContext *dc, char *file, int line)
{
	printf ("BUG: pc=%x %s %d\n", dc->pc, file, line);
	fprintf (logfile, "BUG: pc=%x %s %d\n", dc->pc, file, line);
	cpu_dump_state (dc->env, stdout, fprintf, 0);
	fflush(NULL);
	cris_prepare_jmp (dc, 0x70000000 + line);
}

/* Table to generate quick moves from T0 onto any register.  */
static GenOpFunc *gen_movl_reg_T0[16] =
{
	gen_op_movl_r0_T0, gen_op_movl_r1_T0,
	gen_op_movl_r2_T0, gen_op_movl_r3_T0,
	gen_op_movl_r4_T0, gen_op_movl_r5_T0,
	gen_op_movl_r6_T0, gen_op_movl_r7_T0,
	gen_op_movl_r8_T0, gen_op_movl_r9_T0,
	gen_op_movl_r10_T0, gen_op_movl_r11_T0,
	gen_op_movl_r12_T0, gen_op_movl_r13_T0,
	gen_op_movl_r14_T0, gen_op_movl_r15_T0,
};
static GenOpFunc *gen_movl_T0_reg[16] =
{
	gen_op_movl_T0_r0, gen_op_movl_T0_r1,
	gen_op_movl_T0_r2, gen_op_movl_T0_r3,
	gen_op_movl_T0_r4, gen_op_movl_T0_r5,
	gen_op_movl_T0_r6, gen_op_movl_T0_r7,
	gen_op_movl_T0_r8, gen_op_movl_T0_r9,
	gen_op_movl_T0_r10, gen_op_movl_T0_r11,
	gen_op_movl_T0_r12, gen_op_movl_T0_r13,
	gen_op_movl_T0_r14, gen_op_movl_T0_r15,
};

static void noop_write(void) {
	/* nop.  */
}

static void gen_vr_read(void) {
	gen_op_movl_T0_im(32);
}

static void gen_ccs_read(void) {
	gen_op_movl_T0_p13();
}

static void gen_ccs_write(void) {
	gen_op_movl_p13_T0();
}

/* Table to generate quick moves from T0 onto any register.  */
static GenOpFunc *gen_movl_preg_T0[16] =
{
	noop_write,  /* bz, not writeable.  */
	noop_write,  /* vr, not writeable.  */
	gen_op_movl_p2_T0, gen_op_movl_p3_T0,
	noop_write,  /* wz, not writeable.  */
	gen_op_movl_p5_T0,
	gen_op_movl_p6_T0, gen_op_movl_p7_T0,
	noop_write,  /* dz, not writeable.  */
	gen_op_movl_p9_T0,
	gen_op_movl_p10_T0, gen_op_movl_p11_T0,
	gen_op_movl_p12_T0,
	gen_ccs_write, /* ccs needs special treatment.  */
	gen_op_movl_p14_T0, gen_op_movl_p15_T0,
};
static GenOpFunc *gen_movl_T0_preg[16] =
{
	gen_op_movl_T0_p0,
	gen_vr_read,
	gen_op_movl_T0_p2, gen_op_movl_T0_p3,
	gen_op_movl_T0_p4, gen_op_movl_T0_p5,
	gen_op_movl_T0_p6, gen_op_movl_T0_p7,
	gen_op_movl_T0_p8, gen_op_movl_T0_p9,
	gen_op_movl_T0_p10, gen_op_movl_T0_p11,
	gen_op_movl_T0_p12,
	gen_ccs_read, /* ccs needs special treatment.  */
	gen_op_movl_T0_p14, gen_op_movl_T0_p15,
};

/* We need this table to handle moves with implicit width.  */
int preg_sizes[] = {
	1, /* bz.  */
	1, /* vr.  */
	4, /* pid.  */
	1, /* srs.  */
	2, /* wz.  */
	4, 4, 4,
	4, 4, 4, 4,
	4, 4, 4, 4,
};

#ifdef CONFIG_USER_ONLY
#define GEN_OP_LD(width, reg) \
  void gen_op_ld##width##_T0_##reg (DisasContext *dc) { \
    gen_op_ld##width##_T0_##reg##_raw(); \
  }
#define GEN_OP_ST(width, reg) \
  void gen_op_st##width##_##reg##_T1 (DisasContext *dc) { \
    gen_op_st##width##_##reg##_T1_raw(); \
  }
#else
#define GEN_OP_LD(width, reg) \
  void gen_op_ld##width##_T0_##reg (DisasContext *dc) { \
    if (dc->memidx) gen_op_ld##width##_T0_##reg##_kernel(); \
    else gen_op_ld##width##_T0_##reg##_user();\
  }
#define GEN_OP_ST(width, reg) \
  void gen_op_st##width##_##reg##_T1 (DisasContext *dc) { \
    if (dc->memidx) gen_op_st##width##_##reg##_T1_kernel(); \
    else gen_op_st##width##_##reg##_T1_user();\
  }
#endif

GEN_OP_LD(ub, T0)
GEN_OP_LD(b, T0)
GEN_OP_ST(b, T0)
GEN_OP_LD(uw, T0)
GEN_OP_LD(w, T0)
GEN_OP_ST(w, T0)
GEN_OP_LD(l, T0)
GEN_OP_ST(l, T0)

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
	TranslationBlock *tb;
	tb = dc->tb;
	if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
		if (n == 0)
			gen_op_goto_tb0(TBPARAM(tb));
		else
			gen_op_goto_tb1(TBPARAM(tb));
		gen_op_movl_T0_0();
	} else {
		gen_op_movl_T0_0();
	}
	gen_op_exit_tb();
}

/* Sign extend at translation time.  */
static int sign_extend(unsigned int val, unsigned int width)
{
	int sval;

	/* LSL.  */
	val <<= 31 - width;
	sval = val;
	/* ASR.  */
	sval >>= 31 - width;
	return sval;
}

static void cris_evaluate_flags(DisasContext *dc)
{
	if (!dc->flags_live) {

		switch (dc->cc_op)
		{
			case CC_OP_MCP:
				gen_op_evaluate_flags_mcp ();
				break;
			case CC_OP_MULS:
				gen_op_evaluate_flags_muls ();
				break;
			case CC_OP_MULU:
				gen_op_evaluate_flags_mulu ();
				break;
			case CC_OP_MOVE:
				switch (dc->cc_size)
				{
					case 4:
						gen_op_evaluate_flags_move_4();
						break;
					case 2:
						gen_op_evaluate_flags_move_2();
						break;
					default:
						gen_op_evaluate_flags ();
						break;
				}
				break;

			default:
			{
				switch (dc->cc_size)
				{
					case 4:
						gen_op_evaluate_flags_alu_4 ();
						break;
					default:
						gen_op_evaluate_flags ();
						break;
				}
			}
			break;
		}
		dc->flags_live = 1;
	}
}

static void cris_cc_mask(DisasContext *dc, unsigned int mask)
{
	uint32_t ovl;

	ovl = (dc->cc_mask ^ mask) & ~mask;
	if (ovl) {
		/* TODO: optimize this case. It trigs all the time.  */
		cris_evaluate_flags (dc);
	}
	dc->cc_mask = mask;

	dc->update_cc = 1;
	if (mask == 0)
		dc->update_cc = 0;
	else {
		gen_op_update_cc_mask(mask);
		dc->flags_live = 0;
	}
}

static void cris_update_cc_op(DisasContext *dc, int op)
{
	dc->cc_op = op;
	gen_op_update_cc_op(op);
	dc->flags_live = 0;
}
static void cris_update_cc_size(DisasContext *dc, int size)
{
	dc->cc_size = size;
	gen_op_update_cc_size_im(size);
}

/* op is the operation.
   T0, T1 are the operands.
   dst is the destination reg.
*/
static void crisv32_alu_op(DisasContext *dc, int op, int rd, int size)
{
	int writeback = 1;
	if (dc->update_cc) {
		cris_update_cc_op(dc, op);
		cris_update_cc_size(dc, size);
		gen_op_update_cc_x(dc->flagx_live, dc->flags_x);
		gen_op_update_cc_dest_T0();
	}

	/* Emit the ALU insns.  */
	switch (op)
	{
		case CC_OP_ADD:
			gen_op_addl_T0_T1();
			/* Extended arithmetics.  */
			if (!dc->flagx_live)
				gen_op_addxl_T0_C();
			else if (dc->flags_x)
				gen_op_addxl_T0_C();
			break;
		case CC_OP_ADDC:
			gen_op_addl_T0_T1();
			gen_op_addl_T0_C();
			break;
		case CC_OP_MCP:
			gen_op_addl_T0_T1();
			gen_op_addl_T0_R();
			break;
		case CC_OP_SUB:
			gen_op_negl_T1_T1();
			gen_op_addl_T0_T1();
			/* CRIS flag evaluation needs ~src.  */
			gen_op_negl_T1_T1();
			gen_op_not_T1_T1();

			/* Extended arithmetics.  */
			if (!dc->flagx_live)
				gen_op_subxl_T0_C();
			else if (dc->flags_x)
				gen_op_subxl_T0_C();
			break;
		case CC_OP_MOVE:
			gen_op_movl_T0_T1();
			break;
		case CC_OP_OR:
			gen_op_orl_T0_T1();
			break;
		case CC_OP_AND:
			gen_op_andl_T0_T1();
			break;
		case CC_OP_XOR:
			gen_op_xorl_T0_T1();
			break;
		case CC_OP_LSL:
			gen_op_lsll_T0_T1();
			break;
		case CC_OP_LSR:
			gen_op_lsrl_T0_T1();
			break;
		case CC_OP_ASR:
			gen_op_asrl_T0_T1();
			break;
		case CC_OP_NEG:
			gen_op_negl_T0_T1();
			/* Extended arithmetics.  */
			gen_op_subxl_T0_C();
			break;
		case CC_OP_LZ:
			gen_op_lz_T0_T1();
			break;
		case CC_OP_BTST:
			gen_op_btst_T0_T1();
			writeback = 0;
			break;
		case CC_OP_MULS:
			gen_op_muls_T0_T1();
			break;
		case CC_OP_MULU:
			gen_op_mulu_T0_T1();
			break;
		case CC_OP_DSTEP:
			gen_op_dstep_T0_T1();
			break;
		case CC_OP_BOUND:
			gen_op_bound_T0_T1();
			break;
		case CC_OP_CMP:
			gen_op_negl_T1_T1();
			gen_op_addl_T0_T1();
			/* CRIS flag evaluation needs ~src.  */
			gen_op_negl_T1_T1();
			gen_op_not_T1_T1();

			/* Extended arithmetics.  */
			gen_op_subxl_T0_C();
			writeback = 0;
			break;
		default:
			fprintf (logfile, "illegal ALU op.\n");
			BUG();
			break;
	}

	if (dc->update_cc)
		gen_op_update_cc_src_T1();

	if (size == 1)
		gen_op_andl_T0_im(0xff);
	else if (size == 2)
		gen_op_andl_T0_im(0xffff);
	/* Writeback.  */
	if (writeback) {
		if (size == 4)
			gen_movl_reg_T0[rd]();
		else {
			gen_op_movl_T1_T0();
			gen_movl_T0_reg[rd]();
			if (size == 1)
				gen_op_andl_T0_im(~0xff);
			else
				gen_op_andl_T0_im(~0xffff);
			gen_op_orl_T0_T1();
			gen_movl_reg_T0[rd]();
			gen_op_movl_T0_T1();
		}
	}
	if (dc->update_cc)
		gen_op_update_cc_result_T0();

	{
		/* TODO: Optimize this.  */
		if (!dc->flagx_live)
			cris_evaluate_flags(dc);
	}
}

static int arith_cc(DisasContext *dc)
{
	if (dc->update_cc) {
		switch (dc->cc_op) {
			case CC_OP_ADD: return 1;
			case CC_OP_SUB: return 1;
			case CC_OP_LSL: return 1;
			case CC_OP_LSR: return 1;
			case CC_OP_ASR: return 1;
			case CC_OP_CMP: return 1;
			default:
				return 0;
		}
	}
	return 0;
}

static void gen_tst_cc (DisasContext *dc, int cond)
{
	int arith_opt;

	/* TODO: optimize more condition codes.  */
	arith_opt = arith_cc(dc) && !dc->flags_live;
	switch (cond) {
		case CC_EQ:
			if (arith_opt)
				gen_op_tst_cc_eq_fast ();
			else {
				cris_evaluate_flags(dc);
				gen_op_tst_cc_eq ();
			}
			break;
		case CC_NE:
			if (arith_opt)
				gen_op_tst_cc_ne_fast ();
			else {
				cris_evaluate_flags(dc);
				gen_op_tst_cc_ne ();
			}
			break;
		case CC_CS:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_cs ();
			break;
		case CC_CC:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_cc ();
			break;
		case CC_VS:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_vs ();
			break;
		case CC_VC:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_vc ();
			break;
		case CC_PL:
			if (arith_opt)
				gen_op_tst_cc_pl_fast ();
			else {
				cris_evaluate_flags(dc);
				gen_op_tst_cc_pl ();
			}
			break;
		case CC_MI:
			if (arith_opt)
				gen_op_tst_cc_mi_fast ();
			else {
				cris_evaluate_flags(dc);
				gen_op_tst_cc_mi ();
			}
			break;
		case CC_LS:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_ls ();
			break;
		case CC_HI:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_hi ();
			break;
		case CC_GE:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_ge ();
			break;
		case CC_LT:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_lt ();
			break;
		case CC_GT:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_gt ();
			break;
		case CC_LE:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_le ();
			break;
		case CC_P:
			cris_evaluate_flags(dc);
			gen_op_tst_cc_p ();
			break;
		case CC_A:
			cris_evaluate_flags(dc);
			gen_op_movl_T0_im (1);
			break;
		default:
			BUG();
			break;
	};
}

static void cris_prepare_cc_branch (DisasContext *dc, int offset, int cond)
{
	/* This helps us re-schedule the micro-code to insns in delay-slots
	   before the actual jump.  */
	dc->delayed_branch = 2;
	dc->delayed_pc = dc->pc + offset;
	dc->bcc = cond;
	if (cond != CC_A)
	{
		gen_tst_cc (dc, cond);
		gen_op_evaluate_bcc ();
	}
	gen_op_movl_T0_im (dc->delayed_pc);
	gen_op_movl_btarget_T0 ();
}

/* Dynamic jumps, when the dest is in a live reg for example.  */
void cris_prepare_dyn_jmp (DisasContext *dc)
{
	/* This helps us re-schedule the micro-code to insns in delay-slots
	   before the actual jump.  */
	dc->delayed_branch = 2;
	dc->dyn_jmp = 1;
	dc->bcc = CC_A;
}

void cris_prepare_jmp (DisasContext *dc, uint32_t dst)
{
	/* This helps us re-schedule the micro-code to insns in delay-slots
	   before the actual jump.  */
	dc->delayed_branch = 2;
	dc->delayed_pc = dst;
	dc->dyn_jmp = 0;
	dc->bcc = CC_A;
}

void gen_load_T0_T0 (DisasContext *dc, unsigned int size, int sign)
{
	if (size == 1) {
		if (sign)
			gen_op_ldb_T0_T0(dc);
		else
			gen_op_ldub_T0_T0(dc);
	}
	else if (size == 2) {
		if (sign)
			gen_op_ldw_T0_T0(dc);
		else
			gen_op_lduw_T0_T0(dc);
	}
	else {
		gen_op_ldl_T0_T0(dc);
	}
}

void gen_store_T0_T1 (DisasContext *dc, unsigned int size)
{
	/* Remember, operands are flipped. CRIS has reversed order.  */
	if (size == 1) {
		gen_op_stb_T0_T1(dc);
	}
	else if (size == 2) {
		gen_op_stw_T0_T1(dc);
	}
	else
		gen_op_stl_T0_T1(dc);
}

/* sign extend T1 according to size.  */
static void gen_sext_T1_T0(int size)
{
	if (size == 1)
		gen_op_extb_T1_T0();
	else if (size == 2)
		gen_op_extw_T1_T0();
}

static void gen_sext_T1_T1(int size)
{
	if (size == 1)
		gen_op_extb_T1_T1();
	else if (size == 2)
		gen_op_extw_T1_T1();
}

static void gen_sext_T0_T0(int size)
{
	if (size == 1)
		gen_op_extb_T0_T0();
	else if (size == 2)
		gen_op_extw_T0_T0();
}

static void gen_zext_T0_T0(int size)
{
	if (size == 1)
		gen_op_zextb_T0_T0();
	else if (size == 2)
		gen_op_zextw_T0_T0();
}

static void gen_zext_T1_T0(int size)
{
	if (size == 1)
		gen_op_zextb_T1_T0();
	else if (size == 2)
		gen_op_zextw_T1_T0();
}

static void gen_zext_T1_T1(int size)
{
	if (size == 1)
		gen_op_zextb_T1_T1();
	else if (size == 2)
		gen_op_zextw_T1_T1();
}

#if DISAS_CRIS
static char memsize_char(int size)
{
	switch (size)
	{
		case 1: return 'b';  break;
		case 2: return 'w';  break;
		case 4: return 'd';  break;
		default:
			return 'x';
			break;
	}
}
#endif

static unsigned int memsize_z(DisasContext *dc)
{
	return dc->zsize + 1;
}

static unsigned int memsize_zz(DisasContext *dc)
{
	switch (dc->zzsize)
	{
		case 0: return 1;
		case 1: return 2;
		default:
			return 4;
	}
}

static void do_postinc (DisasContext *dc, int size)
{
	if (!dc->postinc)
		return;
	gen_movl_T0_reg[dc->op1]();
	gen_op_addl_T0_im(size);
	gen_movl_reg_T0[dc->op1]();
}


static void dec_prep_move_r(DisasContext *dc, int rs, int rd,
			    int size, int s_ext)
{
	gen_movl_T0_reg[rs]();
	gen_op_movl_T1_T0();
	if (s_ext)
		gen_sext_T1_T1(size);
	else
		gen_zext_T1_T1(size);
}

/* Prepare T0 and T1 for a register alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static void dec_prep_alu_r(DisasContext *dc, int rs, int rd,
			  int size, int s_ext)
{
	dec_prep_move_r(dc, rs, rd, size, s_ext);

	gen_movl_T0_reg[rd]();
	if (s_ext)
		gen_sext_T0_T0(size);
	else
		gen_zext_T0_T0(size);
}

/* Prepare T0 and T1 for a memory + alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static int dec_prep_alu_m(DisasContext *dc, int s_ext, int memsize)
{
	unsigned int rs, rd;
	uint32_t imm;
	int is_imm;
	int insn_len = 2;

	rs = dc->op1;
	rd = dc->op2;
	is_imm = rs == 15 && dc->postinc;

	/* Load [$rs] onto T1.  */
	if (is_imm) {
		insn_len = 2 + memsize;
		if (memsize == 1)
			insn_len++;

		imm = ldl_code(dc->pc + 2);
		if (memsize != 4) {
			if (s_ext) {
				imm = sign_extend(imm, (memsize * 8) - 1);
			} else {
				if (memsize == 1)
					imm &= 0xff;
				else
					imm &= 0xffff;
			}
		}
		DIS(fprintf (logfile, "imm=%x rd=%d sext=%d ms=%d\n",
			    imm, rd, s_ext, memsize));
		gen_op_movl_T1_im (imm);
		dc->postinc = 0;
	} else {
		gen_movl_T0_reg[rs]();
		gen_load_T0_T0(dc, memsize, 0);
		gen_op_movl_T1_T0();
		if (s_ext)
			gen_sext_T1_T1(memsize);
		else
			gen_zext_T1_T1(memsize);
	}

	/* put dest in T0.  */
	gen_movl_T0_reg[rd]();
	return insn_len;
}

#if DISAS_CRIS
static const char *cc_name(int cc)
{
	static char *cc_names[16] = {
		"cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi",
		"ls", "hi", "ge", "lt", "gt", "le", "a", "p"
	};
	assert(cc < 16);
	return cc_names[cc];
}
#endif

static unsigned int dec_bccq(DisasContext *dc)
{
	int32_t offset;
	int sign;
	uint32_t cond = dc->op2;
	int tmp;

	offset = EXTRACT_FIELD (dc->ir, 1, 7);
	sign = EXTRACT_FIELD(dc->ir, 0, 0);

	offset *= 2;
	offset |= sign << 8;
	tmp = offset;
	offset = sign_extend(offset, 8);

	/* op2 holds the condition-code.  */
	cris_cc_mask(dc, 0);
	cris_prepare_cc_branch (dc, offset, cond);
	return 2;
}
static unsigned int dec_addoq(DisasContext *dc)
{
	uint32_t imm;

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 7);
	imm = sign_extend(dc->op1, 7);

	DIS(fprintf (logfile, "addoq %d, $r%u\n", imm, dc->op2));
	cris_cc_mask(dc, 0);
	/* Fetch register operand,  */
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(imm);
	crisv32_alu_op(dc, CC_OP_ADD, REG_ACR, 4);
	return 2;
}
static unsigned int dec_addq(DisasContext *dc)
{
	DIS(fprintf (logfile, "addq %u, $r%u\n", dc->op1, dc->op2));

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	cris_cc_mask(dc, CC_MASK_NZVC);
	/* Fetch register operand,  */
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	return 2;
}
static unsigned int dec_moveq(DisasContext *dc)
{
	uint32_t imm;

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);
	DIS(fprintf (logfile, "moveq %d, $r%u\n", imm, dc->op2));

	cris_cc_mask(dc, 0);
	gen_op_movl_T1_im(imm);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);

	return 2;
}
static unsigned int dec_subq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	DIS(fprintf (logfile, "subq %u, $r%u\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	/* Fetch register operand,  */
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, 4);
	return 2;
}
static unsigned int dec_cmpq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);

	DIS(fprintf (logfile, "cmpq %d, $r%d\n", imm, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZVC);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(imm);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, 4);
	return 2;
}
static unsigned int dec_andq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);

	DIS(fprintf (logfile, "andq %d, $r%d\n", imm, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(imm);
	crisv32_alu_op(dc, CC_OP_AND, dc->op2, 4);
	return 2;
}
static unsigned int dec_orq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);
	DIS(fprintf (logfile, "orq %d, $r%d\n", imm, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(imm);
	crisv32_alu_op(dc, CC_OP_OR, dc->op2, 4);
	return 2;
}
static unsigned int dec_btstq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "btstq %u, $r%d\n", dc->op1, dc->op2));
	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_BTST, dc->op2, 4);

	cris_update_cc_op(dc, CC_OP_FLAGS);
	gen_op_movl_flags_T0();
	dc->flags_live = 1;
	return 2;
}
static unsigned int dec_asrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "asrq %u, $r%d\n", dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_ASR, dc->op2, 4);
	return 2;
}
static unsigned int dec_lslq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "lslq %u, $r%d\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_LSL, dc->op2, 4);
	return 2;
}
static unsigned int dec_lsrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "lsrq %u, $r%d\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_im(dc->op1);
	crisv32_alu_op(dc, CC_OP_LSR, dc->op2, 4);
	return 2;
}

static unsigned int dec_move_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "move.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_move_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, size);
	return 2;
}

static unsigned int dec_scc_r(DisasContext *dc)
{
	int cond = dc->op2;

	DIS(fprintf (logfile, "s%s $r%u\n",
		    cc_name(cond), dc->op1));

	if (cond != CC_A)
	{
		gen_tst_cc (dc, cond);
		gen_op_movl_T1_T0();
	}
	else
		gen_op_movl_T1_im(1);

	cris_cc_mask(dc, 0);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op1, 4);
	return 2;
}

static unsigned int dec_and_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "and.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_AND, dc->op2, size);
	return 2;
}

static unsigned int dec_lz_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "lz $r%u, $r%u\n",
		    dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	crisv32_alu_op(dc, CC_OP_LZ, dc->op2, 4);
	return 2;
}

static unsigned int dec_lsl_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "lsl.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	gen_op_andl_T1_im(63);
	crisv32_alu_op(dc, CC_OP_LSL, dc->op2, size);
	return 2;
}

static unsigned int dec_lsr_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "lsr.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	gen_op_andl_T1_im(63);
	crisv32_alu_op(dc, CC_OP_LSR, dc->op2, size);
	return 2;
}

static unsigned int dec_asr_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "asr.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1);
	gen_op_andl_T1_im(63);
	crisv32_alu_op(dc, CC_OP_ASR, dc->op2, size);
	return 2;
}

static unsigned int dec_muls_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "muls.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZV);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1);
	gen_sext_T0_T0(size);
	crisv32_alu_op(dc, CC_OP_MULS, dc->op2, 4);
	return 2;
}

static unsigned int dec_mulu_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	DIS(fprintf (logfile, "mulu.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZV);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	gen_zext_T0_T0(size);
	crisv32_alu_op(dc, CC_OP_MULU, dc->op2, 4);
	return 2;
}


static unsigned int dec_dstep_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "dstep $r%u, $r%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_T1_T0();
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_DSTEP, dc->op2, 4);
	return 2;
}

static unsigned int dec_xor_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "xor.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	BUG_ON(size != 4); /* xor is dword.  */
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_XOR, dc->op2, 4);
	return 2;
}

static unsigned int dec_bound_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "bound.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	/* TODO: needs optmimization.  */
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	/* rd should be 4.  */
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_BOUND, dc->op2, 4);
	return 2;
}

static unsigned int dec_cmp_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "cmp.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZVC);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, size);
	return 2;
}

static unsigned int dec_abs_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "abs $r%u, $r%u\n",
		    dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_move_r(dc, dc->op1, dc->op2, 4, 0);
	gen_op_absl_T1_T1();
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}

static unsigned int dec_add_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "add.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZVC);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, size);
	return 2;
}

static unsigned int dec_addc_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "addc $r%u, $r%u\n",
		    dc->op1, dc->op2));
	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_NZVC);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	crisv32_alu_op(dc, CC_OP_ADDC, dc->op2, 4);
	return 2;
}

static unsigned int dec_mcp_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "mcp $p%u, $r%u\n",
		     dc->op2, dc->op1));
	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_RNZV);
	gen_movl_T0_preg[dc->op2]();
	gen_op_movl_T1_T0();
	gen_movl_T0_reg[dc->op1]();
	crisv32_alu_op(dc, CC_OP_MCP, dc->op1, 4);
	return 2;
}

#if DISAS_CRIS
static char * swapmode_name(int mode, char *modename) {
	int i = 0;
	if (mode & 8)
		modename[i++] = 'n';
	if (mode & 4)
		modename[i++] = 'w';
	if (mode & 2)
		modename[i++] = 'b';
	if (mode & 1)
		modename[i++] = 'r';
	modename[i++] = 0;
	return modename;
}
#endif

static unsigned int dec_swap_r(DisasContext *dc)
{
	DIS(char modename[4]);
	DIS(fprintf (logfile, "swap%s $r%u\n",
		     swapmode_name(dc->op2, modename), dc->op1));

	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op1]();
	if (dc->op2 & 8)
		gen_op_not_T0_T0();
	if (dc->op2 & 4)
		gen_op_swapw_T0_T0();
	if (dc->op2 & 2)
		gen_op_swapb_T0_T0();
	if (dc->op2 & 1)
		gen_op_swapr_T0_T0();
	gen_op_movl_T1_T0();
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op1, 4);
	return 2;
}

static unsigned int dec_or_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "or.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_OR, dc->op2, size);
	return 2;
}

static unsigned int dec_addi_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "addi.%c $r%u, $r%u\n",
		    memsize_char(memsize_zz(dc)), dc->op2, dc->op1));
	cris_cc_mask(dc, 0);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	gen_op_lsll_T0_im(dc->zzsize);
	gen_op_addl_T0_T1();
	gen_movl_reg_T0[dc->op1]();
	return 2;
}

static unsigned int dec_addi_acr(DisasContext *dc)
{
	DIS(fprintf (logfile, "addi.%c $r%u, $r%u, $acr\n",
		    memsize_char(memsize_zz(dc)), dc->op2, dc->op1));
	cris_cc_mask(dc, 0);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	gen_op_lsll_T0_im(dc->zzsize);
	gen_op_addl_T0_T1();
	gen_movl_reg_T0[REG_ACR]();
	return 2;
}

static unsigned int dec_neg_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "neg.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZVC);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_NEG, dc->op2, size);
	return 2;
}

static unsigned int dec_btst_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "btst $r%u, $r%u\n",
		    dc->op1, dc->op2));
	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	crisv32_alu_op(dc, CC_OP_BTST, dc->op2, 4);

	cris_update_cc_op(dc, CC_OP_FLAGS);
	gen_op_movl_flags_T0();
	dc->flags_live = 1;
	return 2;
}

static unsigned int dec_sub_r(DisasContext *dc)
{
	int size = memsize_zz(dc);
	DIS(fprintf (logfile, "sub.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZVC);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, size);
	return 2;
}

/* Zero extension. From size to dword.  */
static unsigned int dec_movu_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "movu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_move_r(dc, dc->op1, dc->op2, size, 0);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}

/* Sign extension. From size to dword.  */
static unsigned int dec_movs_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "movs.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	gen_movl_T0_reg[dc->op1]();
	/* Size can only be qi or hi.  */
	gen_sext_T1_T0(size);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}

/* zero extension. From size to dword.  */
static unsigned int dec_addu_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "addu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	gen_movl_T0_reg[dc->op1]();
	/* Size can only be qi or hi.  */
	gen_zext_T1_T0(size);
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	return 2;
}
/* Sign extension. From size to dword.  */
static unsigned int dec_adds_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "adds.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	gen_movl_T0_reg[dc->op1]();
	/* Size can only be qi or hi.  */
	gen_sext_T1_T0(size);
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	return 2;
}

/* Zero extension. From size to dword.  */
static unsigned int dec_subu_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "subu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	gen_movl_T0_reg[dc->op1]();
	/* Size can only be qi or hi.  */
	gen_zext_T1_T0(size);
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, 4);
	return 2;
}

/* Sign extension. From size to dword.  */
static unsigned int dec_subs_r(DisasContext *dc)
{
	int size = memsize_z(dc);
	DIS(fprintf (logfile, "subs.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	gen_movl_T0_reg[dc->op1]();
	/* Size can only be qi or hi.  */
	gen_sext_T1_T0(size);
	gen_movl_T0_reg[dc->op2]();
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, 4);
	return 2;
}

static unsigned int dec_setclrf(DisasContext *dc)
{
	uint32_t flags;
	int set = (~dc->opcode >> 2) & 1;

	flags = (EXTRACT_FIELD(dc->ir, 12, 15) << 4)
		| EXTRACT_FIELD(dc->ir, 0, 3);
	DIS(fprintf (logfile, "set=%d flags=%x\n", set, flags));
	if (set && flags == 0)
		DIS(fprintf (logfile, "nop\n"));
	else if (!set && (flags & 0x20))
		DIS(fprintf (logfile, "di\n"));
	else
		DIS(fprintf (logfile, "%sf %x\n",
			    set ? "set" : "clr",
			    flags));

	if (set && (flags & X_FLAG)) {
		dc->flagx_live = 1;
		dc->flags_x = 1;
	}

	/* Simply decode the flags.  */
	cris_evaluate_flags (dc);
	cris_update_cc_op(dc, CC_OP_FLAGS);
	if (set)
		gen_op_setf (flags);
	else
		gen_op_clrf (flags);
	dc->flags_live = 1;
	return 2;
}

static unsigned int dec_move_rs(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $r%u, $s%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_sreg_T0(dc->op2);

	if (dc->op2 == 5) /* srs is checked at runtime.  */
		gen_op_movl_tlb_lo_T0();
	return 2;
}
static unsigned int dec_move_sr(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $s%u, $r%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	gen_op_movl_T0_sreg(dc->op1);
	gen_op_movl_T1_T0();
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}
static unsigned int dec_move_rp(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_T1_T0();
	gen_movl_preg_T0[dc->op2]();
	return 2;
}
static unsigned int dec_move_pr(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $p%u, $r%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	gen_movl_T0_preg[dc->op2]();
	gen_op_movl_T1_T0();
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op1, preg_sizes[dc->op2]);
	return 2;
}

static unsigned int dec_move_mr(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "move.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, memsize);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_movs_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "movs.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	/* sign extend.  */
	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_addu_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "addu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	/* sign extend.  */
	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_adds_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "adds.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	/* sign extend.  */
	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_subu_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "subu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	/* sign extend.  */
	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_subs_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "subs.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	/* sign extend.  */
	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_movu_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;

	DIS(fprintf (logfile, "movu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_cmpu_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "cmpu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_cmps_m(DisasContext *dc)
{
	int memsize = memsize_z(dc);
	int insn_len;
	DIS(fprintf (logfile, "cmps.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_cmp_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "cmp.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_test_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "test.%d [$r%u%s] op2=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	gen_op_clrf(3);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	gen_op_swp_T0_T1();
	gen_op_movl_T1_im(0);
	crisv32_alu_op(dc, CC_OP_CMP, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_and_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "and.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_AND, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_add_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "add.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_addo_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "add.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, 0);
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	crisv32_alu_op(dc, CC_OP_ADD, REG_ACR, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_bound_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "bound.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_BOUND, dc->op2, 4);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_addc_mr(DisasContext *dc)
{
	int insn_len = 2;
	DIS(fprintf (logfile, "addc [$r%u%s, $r%u\n",
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, 4);
	crisv32_alu_op(dc, CC_OP_ADDC, dc->op2, 4);
	do_postinc(dc, 4);
	return insn_len;
}

static unsigned int dec_sub_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "sub.%c [$r%u%s, $r%u ir=%x zz=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2, dc->ir, dc->zzsize));

	cris_cc_mask(dc, CC_MASK_NZVC);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_SUB, dc->op2, memsize);
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_or_m(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	DIS(fprintf (logfile, "or.%d [$r%u%s, $r%u pc=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2, dc->pc));

	cris_cc_mask(dc, CC_MASK_NZ);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	crisv32_alu_op(dc, CC_OP_OR, dc->op2, memsize_zz(dc));
	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_move_mp(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len = 2;

	DIS(fprintf (logfile, "move.%c [$r%u%s, $p%u\n",
		    memsize_char(memsize),
		    dc->op1,
		    dc->postinc ? "+]" : "]",
		    dc->op2));

	cris_cc_mask(dc, 0);
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	gen_op_movl_T0_T1();
	gen_movl_preg_T0[dc->op2]();

	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_move_pm(DisasContext *dc)
{
	int memsize;

	memsize = preg_sizes[dc->op2];

	DIS(fprintf (logfile, "move.%d $p%u, [$r%u%s\n",
		     memsize, dc->op2, dc->op1, dc->postinc ? "+]" : "]"));

	cris_cc_mask(dc, 0);
	/* prepare store.  */
	gen_movl_T0_preg[dc->op2]();
	gen_op_movl_T1_T0();
	gen_movl_T0_reg[dc->op1]();
	gen_store_T0_T1(dc, memsize);
	if (dc->postinc)
	{
		gen_op_addl_T0_im(memsize);
		gen_movl_reg_T0[dc->op1]();
	}
	return 2;
}

static unsigned int dec_movem_mr(DisasContext *dc)
{
	int i;

	DIS(fprintf (logfile, "movem [$r%u%s, $r%u\n", dc->op1,
		    dc->postinc ? "+]" : "]", dc->op2));

	cris_cc_mask(dc, 0);
	/* fetch the address into T1.  */
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_T1_T0();
	for (i = 0; i <= dc->op2; i++) {
		/* Perform the load onto regnum i. Always dword wide.  */
		gen_load_T0_T0(dc, 4, 0);
		gen_movl_reg_T0[i]();
		/* Update the address.  */
		gen_op_addl_T1_im(4);
		gen_op_movl_T0_T1();
	}
	if (dc->postinc) {
		/* writeback the updated pointer value.  */
		gen_movl_reg_T0[dc->op1]();
	}
	return 2;
}

static unsigned int dec_movem_rm(DisasContext *dc)
{
	int i;

	DIS(fprintf (logfile, "movem $r%u, [$r%u%s\n", dc->op2, dc->op1,
		     dc->postinc ? "+]" : "]"));

	cris_cc_mask(dc, 0);
	for (i = 0; i <= dc->op2; i++) {
		/* Fetch register i into T1.  */
		gen_movl_T0_reg[i]();
		gen_op_movl_T1_T0();

		/* Fetch the address into T0.  */
		gen_movl_T0_reg[dc->op1]();
		/* Displace it.  */
		gen_op_addl_T0_im(i * 4);

		/* Perform the store.  */
		gen_store_T0_T1(dc, 4);
	}
	if (dc->postinc) {
		/* Update the address.  */
		gen_op_addl_T0_im(4);
		/* writeback the updated pointer value.  */
		gen_movl_reg_T0[dc->op1]();
	}
	return 2;
}

static unsigned int dec_move_rm(DisasContext *dc)
{
	int memsize;

	memsize = memsize_zz(dc);

	DIS(fprintf (logfile, "move.%d $r%u, [$r%u]\n",
		     memsize, dc->op2, dc->op1));

	cris_cc_mask(dc, 0);
	/* prepare store.  */
	gen_movl_T0_reg[dc->op2]();
	gen_op_movl_T1_T0();
	gen_movl_T0_reg[dc->op1]();
	gen_store_T0_T1(dc, memsize);
	if (dc->postinc)
	{
		gen_op_addl_T0_im(memsize);
		gen_movl_reg_T0[dc->op1]();
	}
	return 2;
}


static unsigned int dec_lapcq(DisasContext *dc)
{
	DIS(fprintf (logfile, "lapcq %x, $r%u\n",
		    dc->pc + dc->op1*2, dc->op2));
	cris_cc_mask(dc, 0);
	gen_op_movl_T1_im(dc->pc + dc->op1*2);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}

static unsigned int dec_lapc_im(DisasContext *dc)
{
	unsigned int rd;
	int32_t imm;
	int insn_len = 6;

	rd = dc->op2;

	cris_cc_mask(dc, 0);
	imm = ldl_code(dc->pc + 2);
	DIS(fprintf (logfile, "lapc 0x%x, $r%u\n", imm + dc->pc, dc->op2));
	gen_op_movl_T0_im (dc->pc + imm);
	gen_movl_reg_T0[rd] ();
	return insn_len;
}

/* Jump to special reg.  */
static unsigned int dec_jump_p(DisasContext *dc)
{
	DIS(fprintf (logfile, "jump $p%u\n", dc->op2));
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	gen_movl_T0_preg[dc->op2]();
	gen_op_movl_btarget_T0();
	cris_prepare_dyn_jmp(dc);
	return 2;
}

/* Jump and save.  */
static unsigned int dec_jas_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "jas $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 4);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 2;
}

static unsigned int dec_jas_im(DisasContext *dc)
{
	uint32_t imm;

	imm = ldl_code(dc->pc + 2);

	DIS(fprintf (logfile, "jas 0x%x\n", imm));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_op_movl_T0_im(imm);
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 8);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_jasc_im(DisasContext *dc)
{
	uint32_t imm;

	imm = ldl_code(dc->pc + 2);

	DIS(fprintf (logfile, "jasc 0x%x\n", imm));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_op_movl_T0_im(imm);
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 8 + 4);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_jasc_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "jasc_r $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_movl_T0_reg[dc->op1]();
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 4 + 4);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 2;
}

static unsigned int dec_bcc_im(DisasContext *dc)
{
	int32_t offset;
	uint32_t cond = dc->op2;

	offset = ldl_code(dc->pc + 2);
	offset = sign_extend(offset, 15);

	DIS(fprintf (logfile, "b%s %d pc=%x dst=%x\n",
		    cc_name(cond), offset,
		    dc->pc, dc->pc + offset));

	cris_cc_mask(dc, 0);
	/* op2 holds the condition-code.  */
	cris_prepare_cc_branch (dc, offset, cond);
	return 4;
}

static unsigned int dec_bas_im(DisasContext *dc)
{
	int32_t simm;


	simm = ldl_code(dc->pc + 2);

	DIS(fprintf (logfile, "bas 0x%x, $p%u\n", dc->pc + simm, dc->op2));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_op_movl_T0_im(dc->pc + simm);
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 8);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_basc_im(DisasContext *dc)
{
	int32_t simm;
	simm = ldl_code(dc->pc + 2);

	DIS(fprintf (logfile, "basc 0x%x, $p%u\n", dc->pc + simm, dc->op2));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	gen_op_movl_T0_im(dc->pc + simm);
	gen_op_movl_btarget_T0();
	gen_op_movl_T0_im(dc->pc + 12);
	gen_movl_preg_T0[dc->op2]();
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_rfe_etc(DisasContext *dc)
{
	DIS(fprintf (logfile, "rfe_etc opc=%x pc=0x%x op1=%d op2=%d\n",
		    dc->opcode, dc->pc, dc->op1, dc->op2));

	cris_cc_mask(dc, 0);

	if (dc->op2 == 15) /* ignore halt.  */
		goto done;

	switch (dc->op2 & 7) {
		case 2:
			/* rfe.  */
			cris_evaluate_flags(dc);
			gen_op_ccs_rshift();
			break;
		case 5:
			/* rfn.  */
			BUG();
			break;
		case 6:
			/* break.  */
			gen_op_movl_T0_im(dc->pc);
			gen_op_movl_pc_T0();
			/* Breaks start at 16 in the exception vector.  */
			gen_op_break_im(dc->op1 + 16);
			break;
		default:
			printf ("op2=%x\n", dc->op2);
			BUG();
			break;

	}
  done:
	return 2;
}

static unsigned int dec_null(DisasContext *dc)
{
	printf ("unknown insn pc=%x opc=%x op1=%x op2=%x\n",
		dc->pc, dc->opcode, dc->op1, dc->op2);
	fflush(NULL);
	BUG();
	return 2;
}

struct decoder_info {
	struct {
		uint32_t bits;
		uint32_t mask;
	};
	unsigned int (*dec)(DisasContext *dc);
} decinfo[] = {
	/* Order matters here.  */
	{DEC_MOVEQ, dec_moveq},
	{DEC_BTSTQ, dec_btstq},
	{DEC_CMPQ, dec_cmpq},
	{DEC_ADDOQ, dec_addoq},
	{DEC_ADDQ, dec_addq},
	{DEC_SUBQ, dec_subq},
	{DEC_ANDQ, dec_andq},
	{DEC_ORQ, dec_orq},
	{DEC_ASRQ, dec_asrq},
	{DEC_LSLQ, dec_lslq},
	{DEC_LSRQ, dec_lsrq},
	{DEC_BCCQ, dec_bccq},

	{DEC_BCC_IM, dec_bcc_im},
	{DEC_JAS_IM, dec_jas_im},
	{DEC_JAS_R, dec_jas_r},
	{DEC_JASC_IM, dec_jasc_im},
	{DEC_JASC_R, dec_jasc_r},
	{DEC_BAS_IM, dec_bas_im},
	{DEC_BASC_IM, dec_basc_im},
	{DEC_JUMP_P, dec_jump_p},
	{DEC_LAPC_IM, dec_lapc_im},
	{DEC_LAPCQ, dec_lapcq},

	{DEC_RFE_ETC, dec_rfe_etc},
	{DEC_ADDC_MR, dec_addc_mr},

	{DEC_MOVE_MP, dec_move_mp},
	{DEC_MOVE_PM, dec_move_pm},
	{DEC_MOVEM_MR, dec_movem_mr},
	{DEC_MOVEM_RM, dec_movem_rm},
	{DEC_MOVE_PR, dec_move_pr},
	{DEC_SCC_R, dec_scc_r},
	{DEC_SETF, dec_setclrf},
	{DEC_CLEARF, dec_setclrf},

	{DEC_MOVE_SR, dec_move_sr},
	{DEC_MOVE_RP, dec_move_rp},
	{DEC_SWAP_R, dec_swap_r},
	{DEC_ABS_R, dec_abs_r},
	{DEC_LZ_R, dec_lz_r},
	{DEC_MOVE_RS, dec_move_rs},
	{DEC_BTST_R, dec_btst_r},
	{DEC_ADDC_R, dec_addc_r},

	{DEC_DSTEP_R, dec_dstep_r},
	{DEC_XOR_R, dec_xor_r},
	{DEC_MCP_R, dec_mcp_r},
	{DEC_CMP_R, dec_cmp_r},

	{DEC_ADDI_R, dec_addi_r},
	{DEC_ADDI_ACR, dec_addi_acr},

	{DEC_ADD_R, dec_add_r},
	{DEC_SUB_R, dec_sub_r},

	{DEC_ADDU_R, dec_addu_r},
	{DEC_ADDS_R, dec_adds_r},
	{DEC_SUBU_R, dec_subu_r},
	{DEC_SUBS_R, dec_subs_r},
	{DEC_LSL_R, dec_lsl_r},

	{DEC_AND_R, dec_and_r},
	{DEC_OR_R, dec_or_r},
	{DEC_BOUND_R, dec_bound_r},
	{DEC_ASR_R, dec_asr_r},
	{DEC_LSR_R, dec_lsr_r},

	{DEC_MOVU_R, dec_movu_r},
	{DEC_MOVS_R, dec_movs_r},
	{DEC_NEG_R, dec_neg_r},
	{DEC_MOVE_R, dec_move_r},

	/* ftag_fidx_i_m.  */
	/* ftag_fidx_d_m.  */

	{DEC_MULS_R, dec_muls_r},
	{DEC_MULU_R, dec_mulu_r},

	{DEC_ADDU_M, dec_addu_m},
	{DEC_ADDS_M, dec_adds_m},
	{DEC_SUBU_M, dec_subu_m},
	{DEC_SUBS_M, dec_subs_m},

	{DEC_CMPU_M, dec_cmpu_m},
	{DEC_CMPS_M, dec_cmps_m},
	{DEC_MOVU_M, dec_movu_m},
	{DEC_MOVS_M, dec_movs_m},

	{DEC_CMP_M, dec_cmp_m},
	{DEC_ADDO_M, dec_addo_m},
	{DEC_BOUND_M, dec_bound_m},
	{DEC_ADD_M, dec_add_m},
	{DEC_SUB_M, dec_sub_m},
	{DEC_AND_M, dec_and_m},
	{DEC_OR_M, dec_or_m},
	{DEC_MOVE_RM, dec_move_rm},
	{DEC_TEST_M, dec_test_m},
	{DEC_MOVE_MR, dec_move_mr},

	{{0, 0}, dec_null}
};

static inline unsigned int
cris_decoder(DisasContext *dc)
{
	unsigned int insn_len = 2;
	uint32_t tmp;
	int i;

	/* Load a halfword onto the instruction register.  */
	tmp = ldl_code(dc->pc);
	dc->ir = tmp & 0xffff;

	/* Now decode it.  */
	dc->opcode   = EXTRACT_FIELD(dc->ir, 4, 11);
	dc->op1      = EXTRACT_FIELD(dc->ir, 0, 3);
	dc->op2      = EXTRACT_FIELD(dc->ir, 12, 15);
	dc->zsize    = EXTRACT_FIELD(dc->ir, 4, 4);
	dc->zzsize   = EXTRACT_FIELD(dc->ir, 4, 5);
	dc->postinc  = EXTRACT_FIELD(dc->ir, 10, 10);

	/* Large switch for all insns.  */
	for (i = 0; i < sizeof decinfo / sizeof decinfo[0]; i++) {
		if ((dc->opcode & decinfo[i].mask) == decinfo[i].bits)
		{
			insn_len = decinfo[i].dec(dc);
			break;
		}
	}

	return insn_len;
}

static void check_breakpoint(CPUState *env, DisasContext *dc)
{
	int j;
	if (env->nb_breakpoints > 0) {
		for(j = 0; j < env->nb_breakpoints; j++) {
			if (env->breakpoints[j] == dc->pc) {
				cris_evaluate_flags (dc);
				gen_op_movl_T0_im((long)dc->pc);
				gen_op_movl_pc_T0();
				gen_op_debug();
				dc->is_jmp = DISAS_UPDATE;
			}
		}
	}
}


/* generate intermediate code for basic block 'tb'.  */
struct DisasContext ctx;
static int
gen_intermediate_code_internal(CPUState *env, TranslationBlock *tb,
                               int search_pc)
{
	uint16_t *gen_opc_end;
   	uint32_t pc_start;
	unsigned int insn_len;
	int j, lj;
	struct DisasContext *dc = &ctx;
	uint32_t next_page_start;

	pc_start = tb->pc;
	dc->env = env;
	dc->tb = tb;

	gen_opc_ptr = gen_opc_buf;
	gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
	gen_opparam_ptr = gen_opparam_buf;

	dc->is_jmp = DISAS_NEXT;
	dc->pc = pc_start;
	dc->singlestep_enabled = env->singlestep_enabled;
	dc->flagx_live = 0;
	dc->flags_x = 0;
	next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
	lj = -1;
	do
	{
		check_breakpoint(env, dc);
		if (dc->is_jmp == DISAS_JUMP)
			goto done;

		if (search_pc) {
			j = gen_opc_ptr - gen_opc_buf;
			if (lj < j) {
				lj++;
				while (lj < j)
					gen_opc_instr_start[lj++] = 0;
			}
			gen_opc_pc[lj] = dc->pc;
			gen_opc_instr_start[lj] = 1;
		}

		insn_len = cris_decoder(dc);
		STATS(gen_op_exec_insn());
		dc->pc += insn_len;
		if (!dc->flagx_live
		    || (dc->flagx_live &&
			!(dc->cc_op == CC_OP_FLAGS && dc->flags_x))) {
			gen_movl_T0_preg[SR_CCS]();
			gen_op_andl_T0_im(~X_FLAG);
			gen_movl_preg_T0[SR_CCS]();
			dc->flagx_live = 1;
			dc->flags_x = 0;
		}

		/* Check for delayed branches here. If we do it before
		   actually genereating any host code, the simulator will just
		   loop doing nothing for on this program location.  */
		if (dc->delayed_branch) {
			dc->delayed_branch--;
			if (dc->delayed_branch == 0)
			{
				if (dc->bcc == CC_A) {
					gen_op_jmp ();
					dc->is_jmp = DISAS_UPDATE;
				}
				else {
					/* Conditional jmp.  */
					gen_op_cc_jmp (dc->delayed_pc, dc->pc);
					dc->is_jmp = DISAS_UPDATE;
				}
			}
		}

		if (env->singlestep_enabled)
			break;
	} while (!dc->is_jmp && gen_opc_ptr < gen_opc_end
		 && dc->pc < next_page_start);

	if (!dc->is_jmp) {
		gen_op_movl_T0_im((long)dc->pc);
		gen_op_movl_pc_T0();
	}

	cris_evaluate_flags (dc);
  done:
	if (__builtin_expect(env->singlestep_enabled, 0)) {
		gen_op_debug();
	} else {
		switch(dc->is_jmp) {
			case DISAS_NEXT:
				gen_goto_tb(dc, 1, dc->pc);
				break;
			default:
			case DISAS_JUMP:
			case DISAS_UPDATE:
				/* indicate that the hash table must be used
				   to find the next TB */
				/* T0 is used to index the jmp tables.  */
				gen_op_movl_T0_0();
				gen_op_exit_tb();
				break;
			case DISAS_TB_JUMP:
				/* nothing more to generate */
				break;
		}
	}
	*gen_opc_ptr = INDEX_op_end;
	if (search_pc) {
		j = gen_opc_ptr - gen_opc_buf;
		lj++;
		while (lj <= j)
			gen_opc_instr_start[lj++] = 0;
	} else {
		tb->size = dc->pc - pc_start;
	}

#ifdef DEBUG_DISAS
	if (loglevel & CPU_LOG_TB_IN_ASM) {
		fprintf(logfile, "--------------\n");
		fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
		target_disas(logfile, pc_start, dc->pc + 4 - pc_start, 0);
		fprintf(logfile, "\n");
		if (loglevel & CPU_LOG_TB_OP) {
			fprintf(logfile, "OP:\n");
			dump_ops(gen_opc_buf, gen_opparam_buf);
			fprintf(logfile, "\n");
		}
	}
#endif
	return 0;
}

int gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state (CPUState *env, FILE *f,
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
	int i;
	uint32_t srs;

	if (!env || !f)
		return;

	cpu_fprintf(f, "PC=%x CCS=%x btaken=%d btarget=%x\n"
		    "cc_op=%d cc_src=%d cc_dest=%d cc_result=%x cc_mask=%x\n"
		    "debug=%x %x %x\n",
		    env->pc, env->pregs[SR_CCS], env->btaken, env->btarget,
		    env->cc_op,
		    env->cc_src, env->cc_dest, env->cc_result, env->cc_mask,
		    env->debug1, env->debug2, env->debug3);

	for (i = 0; i < 16; i++) {
		cpu_fprintf(f, "r%2.2d=%8.8x ", i, env->regs[i]);
		if ((i + 1) % 4 == 0)
			cpu_fprintf(f, "\n");
	}
	cpu_fprintf(f, "\nspecial regs:\n");
	for (i = 0; i < 16; i++) {
		cpu_fprintf(f, "p%2.2d=%8.8x ", i, env->pregs[i]);
		if ((i + 1) % 4 == 0)
			cpu_fprintf(f, "\n");
	}
	srs = env->pregs[SR_SRS];
	cpu_fprintf(f, "\nsupport function regs bank %d:\n", srs);
	if (srs < 256) {
		for (i = 0; i < 16; i++) {
			cpu_fprintf(f, "s%2.2d=%8.8x ",
				    i, env->sregs[srs][i]);
			if ((i + 1) % 4 == 0)
				cpu_fprintf(f, "\n");
		}
	}
	cpu_fprintf(f, "\n\n");

}

CPUCRISState *cpu_cris_init (const char *cpu_model)
{
	CPUCRISState *env;

	env = qemu_mallocz(sizeof(CPUCRISState));
	if (!env)
		return NULL;
	cpu_exec_init(env);
	cpu_reset(env);
	return env;
}

void cpu_reset (CPUCRISState *env)
{
	memset(env, 0, offsetof(CPUCRISState, breakpoints));
	tlb_flush(env, 1);
}
