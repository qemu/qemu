/*
 *  CRIS emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2008 AXIS Communications AB
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
 * FIXME:
 * The condition code translation is in desperate need of attention. It's slow
 * and for system simulation it seems buggy. It sucks.
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
#include "tcg-op.h"
#include "helper.h"
#include "crisv32-decode.h"
#include "qemu-common.h"

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

#define D(x)
#define BUG() (gen_BUG(dc, __FILE__, __LINE__))
#define BUG_ON(x) ({if (x) BUG();})

#define DISAS_SWI 5

/* Used by the decoder.  */
#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

#define CC_MASK_NZ 0xc
#define CC_MASK_NZV 0xe
#define CC_MASK_NZVC 0xf
#define CC_MASK_RNZV 0x10e

TCGv cpu_env;
TCGv cpu_T[2];
TCGv cpu_R[16];
TCGv cpu_PR[16];
TCGv cc_src;
TCGv cc_dest;
TCGv cc_result;
TCGv cc_op;
TCGv cc_size;
TCGv cc_mask;

TCGv env_btarget;
TCGv env_pc;

/* This is the state at translation time.  */
typedef struct DisasContext {
	CPUState *env;
	target_ulong pc, ppc;

	/* Decoder.  */
	uint32_t ir;
	uint32_t opcode;
	unsigned int op1;
	unsigned int op2;
	unsigned int zsize, zzsize;
	unsigned int mode;
	unsigned int postinc;

	int update_cc;
	int cc_op;
	int cc_size;
	uint32_t cc_mask;
	int flags_live; /* Wether or not $ccs is uptodate.  */
	int flagx_live; /* Wether or not flags_x has the x flag known at
			   translation time.  */
	int flags_x;
	int clear_x; /* Clear x after this insn?  */

	int user; /* user or kernel mode.  */
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

const char *regnames[] =
{
	"$r0", "$r1", "$r2", "$r3",
	"$r4", "$r5", "$r6", "$r7",
	"$r8", "$r9", "$r10", "$r11",
	"$r12", "$r13", "$sp", "$acr",
};
const char *pregnames[] =
{
	"$bz", "$vr", "$pid", "$srs",
	"$wz", "$exs", "$eda", "$mof",
	"$dz", "$ebp", "$erp", "$srp",
	"$nrp", "$ccs", "$usp", "$spc",
};

/* We need this table to handle preg-moves with implicit width.  */
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

#define t_gen_mov_TN_env(tn, member) \
 _t_gen_mov_TN_env((tn), offsetof(CPUState, member))
#define t_gen_mov_env_TN(member, tn) \
 _t_gen_mov_env_TN(offsetof(CPUState, member), (tn))

static inline void t_gen_mov_TN_reg(TCGv tn, int r)
{
	if (r < 0 || r > 15)
		fprintf(stderr, "wrong register read $r%d\n", r);
	tcg_gen_mov_tl(tn, cpu_R[r]);
}
static inline void t_gen_mov_reg_TN(int r, TCGv tn)
{
	if (r < 0 || r > 15)
		fprintf(stderr, "wrong register write $r%d\n", r);
	tcg_gen_mov_tl(cpu_R[r], tn);
}

static inline void _t_gen_mov_TN_env(TCGv tn, int offset)
{
	if (offset > sizeof (CPUState))
		fprintf(stderr, "wrong load from env from off=%d\n", offset);
	tcg_gen_ld_tl(tn, cpu_env, offset);
}
static inline void _t_gen_mov_env_TN(int offset, TCGv tn)
{
	if (offset > sizeof (CPUState))
		fprintf(stderr, "wrong store to env at off=%d\n", offset);
	tcg_gen_st_tl(tn, cpu_env, offset);
}

static inline void t_gen_mov_TN_preg(TCGv tn, int r)
{
	if (r < 0 || r > 15)
		fprintf(stderr, "wrong register read $p%d\n", r);
	if (r == PR_BZ || r == PR_WZ || r == PR_DZ)
		tcg_gen_mov_tl(tn, tcg_const_tl(0));
	else if (r == PR_VR)
		tcg_gen_mov_tl(tn, tcg_const_tl(32));
	else if (r == PR_EXS) {
		printf("read from EXS!\n");
		tcg_gen_mov_tl(tn, cpu_PR[r]);
	}
	else if (r == PR_EDA) {
		printf("read from EDA!\n");
		tcg_gen_mov_tl(tn, cpu_PR[r]);
	}
	else
		tcg_gen_mov_tl(tn, cpu_PR[r]);
}
static inline void t_gen_mov_preg_TN(int r, TCGv tn)
{
	if (r < 0 || r > 15)
		fprintf(stderr, "wrong register write $p%d\n", r);
	if (r == PR_BZ || r == PR_WZ || r == PR_DZ)
		return;
	else if (r == PR_SRS)
		tcg_gen_andi_tl(cpu_PR[r], tn, 3);
	else {
		if (r == PR_PID) {
			tcg_gen_helper_0_0(helper_tlb_flush);
		}
		tcg_gen_mov_tl(cpu_PR[r], tn);
	}
}

static inline void t_gen_mov_TN_im(TCGv tn, int32_t val)
{
	tcg_gen_movi_tl(tn, val);
}

static void t_gen_lsl(TCGv d, TCGv a, TCGv b)
{
	int l1;

	l1 = gen_new_label();
	/* Speculative shift. */
	tcg_gen_shl_tl(d, a, b);
	tcg_gen_brcond_tl(TCG_COND_LE, b, tcg_const_tl(31), l1);
	/* Clear dst if shift operands were to large.  */
	tcg_gen_movi_tl(d, 0);
	gen_set_label(l1);
}

static void t_gen_lsr(TCGv d, TCGv a, TCGv b)
{
	int l1;

	l1 = gen_new_label();
	/* Speculative shift. */
	tcg_gen_shr_tl(d, a, b);
	tcg_gen_brcond_tl(TCG_COND_LE, b, tcg_const_tl(31), l1);
	/* Clear dst if shift operands were to large.  */
	tcg_gen_movi_tl(d, 0);
	gen_set_label(l1);
}

static void t_gen_asr(TCGv d, TCGv a, TCGv b)
{
	int l1;

	l1 = gen_new_label();
	/* Speculative shift. */
	tcg_gen_sar_tl(d, a, b);
	tcg_gen_brcond_tl(TCG_COND_LE, b, tcg_const_tl(31), l1);
	/* Clear dst if shift operands were to large.  */
	tcg_gen_sar_tl(d, a, tcg_const_tl(30));
	gen_set_label(l1);
}

/* 64-bit signed mul, lower result in d and upper in d2.  */
static void t_gen_muls(TCGv d, TCGv d2, TCGv a, TCGv b)
{
	TCGv t0, t1;

	t0 = tcg_temp_new(TCG_TYPE_I64);
	t1 = tcg_temp_new(TCG_TYPE_I64);

	tcg_gen_ext32s_i64(t0, a);
	tcg_gen_ext32s_i64(t1, b);
	tcg_gen_mul_i64(t0, t0, t1);

	tcg_gen_trunc_i64_i32(d, t0);
	tcg_gen_shri_i64(t0, t0, 32);
	tcg_gen_trunc_i64_i32(d2, t0);

	tcg_gen_discard_i64(t0);
	tcg_gen_discard_i64(t1);
}

/* 64-bit unsigned muls, lower result in d and upper in d2.  */
static void t_gen_mulu(TCGv d, TCGv d2, TCGv a, TCGv b)
{
	TCGv t0, t1;

	t0 = tcg_temp_new(TCG_TYPE_I64);
	t1 = tcg_temp_new(TCG_TYPE_I64);

	tcg_gen_extu_i32_i64(t0, a);
	tcg_gen_extu_i32_i64(t1, b);
	tcg_gen_mul_i64(t0, t0, t1);

	tcg_gen_trunc_i64_i32(d, t0);
	tcg_gen_shri_i64(t0, t0, 32);
	tcg_gen_trunc_i64_i32(d2, t0);

	tcg_gen_discard_i64(t0);
	tcg_gen_discard_i64(t1);
}

/* 32bit branch-free binary search for counting leading zeros.  */
static void t_gen_lz_i32(TCGv d, TCGv x)
{
	TCGv y, m, n;

	y = tcg_temp_new(TCG_TYPE_I32);
	m = tcg_temp_new(TCG_TYPE_I32);
	n = tcg_temp_new(TCG_TYPE_I32);

	/* y = -(x >> 16)  */
	tcg_gen_shri_i32(y, x, 16);
	tcg_gen_sub_i32(y, tcg_const_i32(0), y);

	/* m = (y >> 16) & 16  */
	tcg_gen_sari_i32(m, y, 16);
	tcg_gen_andi_i32(m, m, 16);

	/* n = 16 - m  */
	tcg_gen_sub_i32(n, tcg_const_i32(16), m);
	/* x = x >> m  */
	tcg_gen_shr_i32(x, x, m);

	/* y = x - 0x100  */
	tcg_gen_subi_i32(y, x, 0x100);
	/* m = (y >> 16) & 8  */
	tcg_gen_sari_i32(m, y, 16);
	tcg_gen_andi_i32(m, m, 8);
	/* n = n + m  */
	tcg_gen_add_i32(n, n, m);
	/* x = x << m  */
	tcg_gen_shl_i32(x, x, m);

	/* y = x - 0x1000  */
	tcg_gen_subi_i32(y, x, 0x1000);
	/* m = (y >> 16) & 4  */
	tcg_gen_sari_i32(m, y, 16);
	tcg_gen_andi_i32(m, m, 4);
	/* n = n + m  */
	tcg_gen_add_i32(n, n, m);
	/* x = x << m  */
	tcg_gen_shl_i32(x, x, m);

	/* y = x - 0x4000  */
	tcg_gen_subi_i32(y, x, 0x4000);
	/* m = (y >> 16) & 2  */
	tcg_gen_sari_i32(m, y, 16);
	tcg_gen_andi_i32(m, m, 2);
	/* n = n + m  */
	tcg_gen_add_i32(n, n, m);
	/* x = x << m  */
	tcg_gen_shl_i32(x, x, m);

	/* y = x >> 14  */
	tcg_gen_shri_i32(y, x, 14);
	/* m = y & ~(y >> 1)  */
	tcg_gen_sari_i32(m, y, 1);
	tcg_gen_xori_i32(m, m, 0xffffffff);
	tcg_gen_and_i32(m, m, y);

	/* d = n + 2 - m  */
	tcg_gen_addi_i32(d, n, 2);
	tcg_gen_sub_i32(d, d, m);

	tcg_gen_discard_i32(y);
	tcg_gen_discard_i32(m);
	tcg_gen_discard_i32(n);
}

static void t_gen_cris_dstep(TCGv d, TCGv s)
{
	int l1;

	l1 = gen_new_label();

	/* 
	 * d <<= 1
	 * if (d >= s)
	 *    d -= s;
	 */
	tcg_gen_shli_tl(d, d, 1);
	tcg_gen_brcond_tl(TCG_COND_LTU, d, s, l1);
	tcg_gen_sub_tl(d, d, s);
	gen_set_label(l1);
}

/* Extended arithmetics on CRIS.  */
static inline void t_gen_add_flag(TCGv d, int flag)
{
	TCGv c;

	c = tcg_temp_new(TCG_TYPE_TL);
	t_gen_mov_TN_preg(c, PR_CCS);
	/* Propagate carry into d.  */
	tcg_gen_andi_tl(c, c, 1 << flag);
	if (flag)
		tcg_gen_shri_tl(c, c, flag);
	tcg_gen_add_tl(d, d, c);
	tcg_gen_discard_tl(c);
}

static inline void t_gen_addx_carry(TCGv d)
{
	TCGv x, c;

	x = tcg_temp_new(TCG_TYPE_TL);
	c = tcg_temp_new(TCG_TYPE_TL);
	t_gen_mov_TN_preg(x, PR_CCS);
	tcg_gen_mov_tl(c, x);

	/* Propagate carry into d if X is set. Branch free.  */
	tcg_gen_andi_tl(c, c, C_FLAG);
	tcg_gen_andi_tl(x, x, X_FLAG);
	tcg_gen_shri_tl(x, x, 4);

	tcg_gen_and_tl(x, x, c);
	tcg_gen_add_tl(d, d, x);        
	tcg_gen_discard_tl(x);
	tcg_gen_discard_tl(c);
}

static inline void t_gen_subx_carry(TCGv d)
{
	TCGv x, c;

	x = tcg_temp_new(TCG_TYPE_TL);
	c = tcg_temp_new(TCG_TYPE_TL);
	t_gen_mov_TN_preg(x, PR_CCS);
	tcg_gen_mov_tl(c, x);

	/* Propagate carry into d if X is set. Branch free.  */
	tcg_gen_andi_tl(c, c, C_FLAG);
	tcg_gen_andi_tl(x, x, X_FLAG);
	tcg_gen_shri_tl(x, x, 4);

	tcg_gen_and_tl(x, x, c);
	tcg_gen_sub_tl(d, d, x);
	tcg_gen_discard_tl(x);
	tcg_gen_discard_tl(c);
}

/* Swap the two bytes within each half word of the s operand.
   T0 = ((T0 << 8) & 0xff00ff00) | ((T0 >> 8) & 0x00ff00ff)  */
static inline void t_gen_swapb(TCGv d, TCGv s)
{
	TCGv t, org_s;

	t = tcg_temp_new(TCG_TYPE_TL);
	org_s = tcg_temp_new(TCG_TYPE_TL);

	/* d and s may refer to the same object.  */
	tcg_gen_mov_tl(org_s, s);
	tcg_gen_shli_tl(t, org_s, 8);
	tcg_gen_andi_tl(d, t, 0xff00ff00);
	tcg_gen_shri_tl(t, org_s, 8);
	tcg_gen_andi_tl(t, t, 0x00ff00ff);
	tcg_gen_or_tl(d, d, t);
	tcg_gen_discard_tl(t);
	tcg_gen_discard_tl(org_s);
}

/* Swap the halfwords of the s operand.  */
static inline void t_gen_swapw(TCGv d, TCGv s)
{
	TCGv t;
	/* d and s refer the same object.  */
	t = tcg_temp_new(TCG_TYPE_TL);
	tcg_gen_mov_tl(t, s);
	tcg_gen_shli_tl(d, t, 16);
	tcg_gen_shri_tl(t, t, 16);
	tcg_gen_or_tl(d, d, t);
	tcg_gen_discard_tl(t);
}

/* Reverse the within each byte.
   T0 = (((T0 << 7) & 0x80808080) |
   ((T0 << 5) & 0x40404040) |
   ((T0 << 3) & 0x20202020) |
   ((T0 << 1) & 0x10101010) |
   ((T0 >> 1) & 0x08080808) |
   ((T0 >> 3) & 0x04040404) |
   ((T0 >> 5) & 0x02020202) |
   ((T0 >> 7) & 0x01010101));
 */
static inline void t_gen_swapr(TCGv d, TCGv s)
{
	struct {
		int shift; /* LSL when positive, LSR when negative.  */
		uint32_t mask;
	} bitrev [] = {
		{7, 0x80808080},
		{5, 0x40404040},
		{3, 0x20202020},
		{1, 0x10101010},
		{-1, 0x08080808},
		{-3, 0x04040404},
		{-5, 0x02020202},
		{-7, 0x01010101}
	};
	int i;
	TCGv t, org_s;

	/* d and s refer the same object.  */
	t = tcg_temp_new(TCG_TYPE_TL);
	org_s = tcg_temp_new(TCG_TYPE_TL);
	tcg_gen_mov_tl(org_s, s);

	tcg_gen_shli_tl(t, org_s,  bitrev[0].shift);
	tcg_gen_andi_tl(d, t,  bitrev[0].mask);
	for (i = 1; i < sizeof bitrev / sizeof bitrev[0]; i++) {
		if (bitrev[i].shift >= 0) {
			tcg_gen_shli_tl(t, org_s,  bitrev[i].shift);
		} else {
			tcg_gen_shri_tl(t, org_s,  -bitrev[i].shift);
		}
		tcg_gen_andi_tl(t, t,  bitrev[i].mask);
		tcg_gen_or_tl(d, d, t);
	}
	tcg_gen_discard_tl(t);
	tcg_gen_discard_tl(org_s);
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
	TranslationBlock *tb;
	tb = dc->tb;
	if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
		tcg_gen_goto_tb(n);
		tcg_gen_movi_tl(env_pc, dest);
		tcg_gen_exit_tb((long)tb + n);
	} else {
		tcg_gen_mov_tl(env_pc, cpu_T[0]);
		tcg_gen_exit_tb(0);
	}
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

static inline void cris_clear_x_flag(DisasContext *dc)
{
	if (!dc->flagx_live 
	    || (dc->flagx_live && dc->flags_x)
	    || dc->cc_op != CC_OP_FLAGS)
		tcg_gen_andi_i32(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~X_FLAG);
	dc->flagx_live = 1;
	dc->flags_x = 0;
}

static void cris_evaluate_flags(DisasContext *dc)
{
	if (!dc->flags_live) {
		tcg_gen_movi_tl(cc_op, dc->cc_op);
		tcg_gen_movi_tl(cc_size, dc->cc_size);
		tcg_gen_movi_tl(cc_mask, dc->cc_mask);

		switch (dc->cc_op)
		{
			case CC_OP_MCP:
				tcg_gen_helper_0_0(helper_evaluate_flags_mcp);
				break;
			case CC_OP_MULS:
				tcg_gen_helper_0_0(helper_evaluate_flags_muls);
				break;
			case CC_OP_MULU:
				tcg_gen_helper_0_0(helper_evaluate_flags_mulu);
				break;
			case CC_OP_MOVE:
				switch (dc->cc_size)
				{
					case 4:
						tcg_gen_helper_0_0(helper_evaluate_flags_move_4);
						break;
					case 2:
						tcg_gen_helper_0_0(helper_evaluate_flags_move_2);
						break;
					default:
						tcg_gen_helper_0_0(helper_evaluate_flags);
						break;
				}
				break;
			case CC_OP_FLAGS:
				/* live.  */
				break;
			default:
			{
				switch (dc->cc_size)
				{
					case 4:
						tcg_gen_helper_0_0(helper_evaluate_flags_alu_4);
						break;
					default:
						tcg_gen_helper_0_0(helper_evaluate_flags);
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

	/* Check if we need to evaluate the condition codes due to 
	   CC overlaying.  */
	ovl = (dc->cc_mask ^ mask) & ~mask;
	if (ovl) {
		/* TODO: optimize this case. It trigs all the time.  */
		cris_evaluate_flags (dc);
	}
	dc->cc_mask = mask;
	dc->update_cc = 1;

	if (mask == 0)
		dc->update_cc = 0;
	else
		dc->flags_live = 0;
}

static void cris_update_cc_op(DisasContext *dc, int op, int size)
{
	dc->cc_op = op;
	dc->cc_size = size;
	dc->flags_live = 0;
}

/* op is the operation.
   T0, T1 are the operands.
   dst is the destination reg.
*/
static void crisv32_alu_op(DisasContext *dc, int op, int rd, int size)
{
	int writeback = 1;
	if (dc->update_cc) {
		cris_update_cc_op(dc, op, size);
		tcg_gen_mov_tl(cc_dest, cpu_T[0]);

		/* FIXME: This shouldn't be needed. But we don't pass the
		 tests without it. Investigate.  */
		t_gen_mov_env_TN(cc_x_live, tcg_const_tl(dc->flagx_live));
		t_gen_mov_env_TN(cc_x, tcg_const_tl(dc->flags_x));
	}

	/* Emit the ALU insns.  */
	switch (op)
	{
		case CC_OP_ADD:
			tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			/* Extended arithmetics.  */
			t_gen_addx_carry(cpu_T[0]);
			break;
		case CC_OP_ADDC:
			tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			t_gen_add_flag(cpu_T[0], 0); /* C_FLAG.  */
			break;
		case CC_OP_MCP:
			tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			t_gen_add_flag(cpu_T[0], 8); /* R_FLAG.  */
			break;
		case CC_OP_SUB:
			tcg_gen_sub_tl(cpu_T[1], tcg_const_tl(0), cpu_T[1]);
			tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			tcg_gen_sub_tl(cpu_T[1], tcg_const_tl(0), cpu_T[1]);
			/* CRIS flag evaluation needs ~src.  */
			tcg_gen_xori_tl(cpu_T[1], cpu_T[1], -1);

			/* Extended arithmetics.  */
			t_gen_subx_carry(cpu_T[0]);
			break;
		case CC_OP_MOVE:
			tcg_gen_mov_tl(cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_OR:
			tcg_gen_or_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_AND:
			tcg_gen_and_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_XOR:
			tcg_gen_xor_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_LSL:
			t_gen_lsl(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_LSR:
			t_gen_lsr(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_ASR:
			t_gen_asr(cpu_T[0], cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_NEG:
			/* Hopefully the TCG backend recognizes this pattern
			   and makes a real neg out of it.  */
			tcg_gen_sub_tl(cpu_T[0], tcg_const_tl(0), cpu_T[1]);
			/* Extended arithmetics.  */
			t_gen_subx_carry(cpu_T[0]);
			break;
		case CC_OP_LZ:
			t_gen_lz_i32(cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_BTST:
			gen_op_btst_T0_T1();
			writeback = 0;
			break;
		case CC_OP_MULS:
		{
			TCGv mof;
			mof = tcg_temp_new(TCG_TYPE_TL);
			t_gen_muls(cpu_T[0], mof, cpu_T[0], cpu_T[1]);
			t_gen_mov_preg_TN(PR_MOF, mof);
			tcg_gen_discard_tl(mof);
		}
		break;
		case CC_OP_MULU:
		{
			TCGv mof;
			mof = tcg_temp_new(TCG_TYPE_TL);
			t_gen_mulu(cpu_T[0], mof, cpu_T[0], cpu_T[1]);
			t_gen_mov_preg_TN(PR_MOF, mof);
			tcg_gen_discard_tl(mof);
		}
		break;
		case CC_OP_DSTEP:
			t_gen_cris_dstep(cpu_T[0], cpu_T[1]);
			break;
		case CC_OP_BOUND:
		{
			int l1;
			l1 = gen_new_label();
			tcg_gen_brcond_tl(TCG_COND_LEU, 
					  cpu_T[0], cpu_T[1], l1);
			tcg_gen_mov_tl(cpu_T[0], cpu_T[1]);
			gen_set_label(l1);
		}
		break;
		case CC_OP_CMP:
			tcg_gen_sub_tl(cpu_T[1], tcg_const_tl(0), cpu_T[1]);
			tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			/* CRIS flag evaluation needs ~src.  */
			tcg_gen_sub_tl(cpu_T[1], tcg_const_tl(0), cpu_T[1]);
			/* CRIS flag evaluation needs ~src.  */
			tcg_gen_xori_tl(cpu_T[1], cpu_T[1], -1);

			/* Extended arithmetics.  */
			t_gen_subx_carry(cpu_T[0]);
			writeback = 0;
			break;
		default:
			fprintf (logfile, "illegal ALU op.\n");
			BUG();
			break;
	}

	if (dc->update_cc)
		tcg_gen_mov_tl(cc_src, cpu_T[1]);

	if (size == 1)
		tcg_gen_andi_tl(cpu_T[0], cpu_T[0], 0xff);
	else if (size == 2)
		tcg_gen_andi_tl(cpu_T[0], cpu_T[0], 0xffff);

	/* Writeback.  */
	if (writeback) {
		if (size == 4)
			t_gen_mov_reg_TN(rd, cpu_T[0]);
		else {
			tcg_gen_mov_tl(cpu_T[1], cpu_T[0]);
			t_gen_mov_TN_reg(cpu_T[0], rd);
			if (size == 1)
				tcg_gen_andi_tl(cpu_T[0], cpu_T[0], ~0xff);
			else
				tcg_gen_andi_tl(cpu_T[0], cpu_T[0], ~0xffff);
			tcg_gen_or_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
			t_gen_mov_reg_TN(rd, cpu_T[0]);
			tcg_gen_mov_tl(cpu_T[0], cpu_T[1]);
		}
	}
	if (dc->update_cc)
		tcg_gen_mov_tl(cc_result, cpu_T[0]);

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
	tcg_gen_movi_tl(env_btarget, dc->delayed_pc);
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

void gen_load(DisasContext *dc, TCGv dst, TCGv addr, 
	      unsigned int size, int sign)
{
	int mem_index = cpu_mmu_index(dc->env);

	/* FIXME: qemu_ld does not act as a barrier?  */
	tcg_gen_helper_0_0(helper_dummy);
	cris_evaluate_flags(dc);
	if (size == 1) {
		if (sign)
			tcg_gen_qemu_ld8s(dst, addr, mem_index);
		else
			tcg_gen_qemu_ld8u(dst, addr, mem_index);
	}
	else if (size == 2) {
		if (sign)
			tcg_gen_qemu_ld16s(dst, addr, mem_index);
		else
			tcg_gen_qemu_ld16u(dst, addr, mem_index);
	}
	else {
		tcg_gen_qemu_ld32s(dst, addr, mem_index);
	}
}

void gen_store_T0_T1 (DisasContext *dc, unsigned int size)
{
	int mem_index = cpu_mmu_index(dc->env);

	/* FIXME: qemu_st does not act as a barrier?  */
	tcg_gen_helper_0_0(helper_dummy);
	cris_evaluate_flags(dc);

	/* Remember, operands are flipped. CRIS has reversed order.  */
	if (size == 1)
		tcg_gen_qemu_st8(cpu_T[1], cpu_T[0], mem_index);
	else if (size == 2)
		tcg_gen_qemu_st16(cpu_T[1], cpu_T[0], mem_index);
	else
		tcg_gen_qemu_st32(cpu_T[1], cpu_T[0], mem_index);
}

static inline void t_gen_sext(TCGv d, TCGv s, int size)
{
	if (size == 1)
		tcg_gen_ext8s_i32(d, s);
	else if (size == 2)
		tcg_gen_ext16s_i32(d, s);
	else
		tcg_gen_mov_tl(d, s);
}

static inline void t_gen_zext(TCGv d, TCGv s, int size)
{
	/* TCG-FIXME: this is not optimal. Many archs have fast zext insns.  */
	if (size == 1)
		tcg_gen_andi_i32(d, s, 0xff);
	else if (size == 2)
		tcg_gen_andi_i32(d, s, 0xffff);
	else
		tcg_gen_mov_tl(d, s);
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

static inline void do_postinc (DisasContext *dc, int size)
{
	if (dc->postinc)
		tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], size);
}


static void dec_prep_move_r(DisasContext *dc, int rs, int rd,
			    int size, int s_ext)
{
	if (s_ext)
		t_gen_sext(cpu_T[1], cpu_R[rs], size);
	else
		t_gen_zext(cpu_T[1], cpu_R[rs], size);
}

/* Prepare T0 and T1 for a register alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static void dec_prep_alu_r(DisasContext *dc, int rs, int rd,
			  int size, int s_ext)
{
	dec_prep_move_r(dc, rs, rd, size, s_ext);

	if (s_ext)
		t_gen_sext(cpu_T[0], cpu_R[rd], size);
	else
		t_gen_zext(cpu_T[0], cpu_R[rd], size);
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
		tcg_gen_movi_tl(cpu_T[1], imm);
		dc->postinc = 0;
	} else {
		gen_load(dc, cpu_T[1], cpu_R[rs], memsize, 0);
		if (s_ext)
			t_gen_sext(cpu_T[1], cpu_T[1], memsize);
		else
			t_gen_zext(cpu_T[1], cpu_T[1], memsize);
	}

	/* put dest in T0.  */
	t_gen_mov_TN_reg(cpu_T[0], rd);
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

/* Start of insn decoders.  */

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
	int32_t imm;

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 7);
	imm = sign_extend(dc->op1, 7);

	DIS(fprintf (logfile, "addoq %d, $r%u\n", imm, dc->op2));
	cris_cc_mask(dc, 0);
	/* Fetch register operand,  */
	tcg_gen_addi_tl(cpu_R[R_ACR], cpu_R[dc->op2], imm);
	return 2;
}
static unsigned int dec_addq(DisasContext *dc)
{
	DIS(fprintf (logfile, "addq %u, $r%u\n", dc->op1, dc->op2));

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	cris_cc_mask(dc, CC_MASK_NZVC);
	/* Fetch register operand,  */
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	tcg_gen_movi_tl(cpu_T[1], dc->op1);
	crisv32_alu_op(dc, CC_OP_ADD, dc->op2, 4);
	return 2;
}
static unsigned int dec_moveq(DisasContext *dc)
{
	uint32_t imm;

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);
	DIS(fprintf (logfile, "moveq %d, $r%u\n", imm, dc->op2));

	t_gen_mov_reg_TN(dc->op2, tcg_const_tl(imm));
	return 2;
}
static unsigned int dec_subq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	DIS(fprintf (logfile, "subq %u, $r%u\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZVC);
	/* Fetch register operand,  */
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], dc->op1);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], imm);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], imm);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], imm);
	crisv32_alu_op(dc, CC_OP_OR, dc->op2, 4);
	return 2;
}
static unsigned int dec_btstq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "btstq %u, $r%d\n", dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], dc->op1);
	crisv32_alu_op(dc, CC_OP_BTST, dc->op2, 4);

	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	t_gen_mov_preg_TN(PR_CCS, cpu_T[0]);
	dc->flags_live = 1;
	return 2;
}
static unsigned int dec_asrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "asrq %u, $r%d\n", dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], dc->op1);
	crisv32_alu_op(dc, CC_OP_ASR, dc->op2, 4);
	return 2;
}
static unsigned int dec_lslq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "lslq %u, $r%d\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], dc->op1);
	crisv32_alu_op(dc, CC_OP_LSL, dc->op2, 4);
	return 2;
}
static unsigned int dec_lsrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	DIS(fprintf (logfile, "lsrq %u, $r%d\n", dc->op1, dc->op2));

	cris_cc_mask(dc, CC_MASK_NZ);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
	t_gen_mov_TN_im(cpu_T[1], dc->op1);
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
		tcg_gen_mov_tl(cpu_T[1], cpu_T[0]);
	}
	else
		tcg_gen_movi_tl(cpu_T[1], 1);

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
	tcg_gen_andi_tl(cpu_T[1], cpu_T[1], 63);
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
	tcg_gen_andi_tl(cpu_T[1], cpu_T[1], 63);
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
	tcg_gen_andi_tl(cpu_T[1], cpu_T[1], 63);
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
	t_gen_sext(cpu_T[0], cpu_T[0], size);
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
	t_gen_zext(cpu_T[0], cpu_T[0], size);
	crisv32_alu_op(dc, CC_OP_MULU, dc->op2, 4);
	return 2;
}


static unsigned int dec_dstep_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "dstep $r%u, $r%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
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
	int l1;

	DIS(fprintf (logfile, "abs $r%u, $r%u\n",
		    dc->op1, dc->op2));
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_move_r(dc, dc->op1, dc->op2, 4, 0);

	/* TODO: consider a branch free approach.  */
	l1 = gen_new_label();
	tcg_gen_brcond_tl(TCG_COND_GE, cpu_T[1], tcg_const_tl(0), l1);
	tcg_gen_sub_tl(cpu_T[1], tcg_const_tl(0), cpu_T[1]);
	gen_set_label(l1);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	t_gen_mov_TN_preg(cpu_T[1], dc->op2);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	if (dc->op2 & 8)
		tcg_gen_xori_tl(cpu_T[0], cpu_T[0], -1);
	if (dc->op2 & 4)
		t_gen_swapw(cpu_T[0], cpu_T[0]);
	if (dc->op2 & 2)
		t_gen_swapb(cpu_T[0], cpu_T[0]);
	if (dc->op2 & 1)
		t_gen_swapr(cpu_T[0], cpu_T[0]);
	tcg_gen_mov_tl(cpu_T[1], cpu_T[0]);
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
	t_gen_lsl(cpu_T[0], cpu_T[0], tcg_const_tl(dc->zzsize));
	tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
	t_gen_mov_reg_TN(dc->op1, cpu_T[0]);
	return 2;
}

static unsigned int dec_addi_acr(DisasContext *dc)
{
	DIS(fprintf (logfile, "addi.%c $r%u, $r%u, $acr\n",
		  memsize_char(memsize_zz(dc)), dc->op2, dc->op1));
	cris_cc_mask(dc, 0);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	t_gen_lsl(cpu_T[0], cpu_T[0], tcg_const_tl(dc->zzsize));
	
	tcg_gen_add_tl(cpu_T[0], cpu_T[0], cpu_T[1]);
	t_gen_mov_reg_TN(R_ACR, cpu_T[0]);
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
	cris_cc_mask(dc, CC_MASK_NZ);
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0);
	crisv32_alu_op(dc, CC_OP_BTST, dc->op2, 4);

	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	t_gen_mov_preg_TN(PR_CCS, cpu_T[0]);
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
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	/* Size can only be qi or hi.  */
	t_gen_sext(cpu_T[1], cpu_T[0], size);
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
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	/* Size can only be qi or hi.  */
	t_gen_zext(cpu_T[1], cpu_T[1], size);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
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
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	/* Size can only be qi or hi.  */
	t_gen_sext(cpu_T[1], cpu_T[1], size);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);

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
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	/* Size can only be qi or hi.  */
	t_gen_zext(cpu_T[1], cpu_T[1], size);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
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
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	/* Size can only be qi or hi.  */
	t_gen_sext(cpu_T[1], cpu_T[1], size);
	t_gen_mov_TN_reg(cpu_T[0], dc->op2);
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
	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	tcg_gen_movi_tl(cc_op, dc->cc_op);

	if (set)
		gen_op_setf(flags);
	else
		gen_op_clrf(flags);
	dc->flags_live = 1;
	dc->clear_x = 0;
	return 2;
}

static unsigned int dec_move_rs(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $r%u, $s%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	gen_op_movl_sreg_T0(dc->op2);

#if !defined(CONFIG_USER_ONLY)
	if (dc->op2 == 6)
		gen_op_movl_tlb_hi_T0();
	else if (dc->op2 == 5) { /* srs is checked at runtime.  */
		tcg_gen_helper_0_1(helper_tlb_update, cpu_T[0]);
		gen_op_movl_tlb_lo_T0();
	}
#endif
	return 2;
}
static unsigned int dec_move_sr(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $s%u, $r%u\n", dc->op2, dc->op1));
	cris_cc_mask(dc, 0);
	gen_op_movl_T0_sreg(dc->op2);
	tcg_gen_mov_tl(cpu_T[1], cpu_T[0]);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op1, 4);
	return 2;
}
static unsigned int dec_move_rp(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);

	if (dc->op2 == PR_CCS) {
		cris_evaluate_flags(dc);
		t_gen_mov_TN_reg(cpu_T[0], dc->op1);
		if (dc->user) {
			/* User space is not allowed to touch all flags.  */
			tcg_gen_andi_tl(cpu_T[0], cpu_T[0], 0x39f);
			tcg_gen_andi_tl(cpu_T[1], cpu_PR[PR_CCS], ~0x39f);
			tcg_gen_or_tl(cpu_T[0], cpu_T[1], cpu_T[0]);
		}
	}
	else
		t_gen_mov_TN_reg(cpu_T[0], dc->op1);

	t_gen_mov_preg_TN(dc->op2, cpu_T[0]);
	if (dc->op2 == PR_CCS) {
		cris_update_cc_op(dc, CC_OP_FLAGS, 4);
		dc->flags_live = 1;
	}
	return 2;
}
static unsigned int dec_move_pr(DisasContext *dc)
{
	DIS(fprintf (logfile, "move $p%u, $r%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	/* Support register 0 is hardwired to zero. 
	   Treat it specially. */
	if (dc->op2 == 0)
		tcg_gen_movi_tl(cpu_T[1], 0);
	else if (dc->op2 == PR_CCS) {
		cris_evaluate_flags(dc);
		t_gen_mov_TN_preg(cpu_T[1], dc->op2);
	} else
		t_gen_mov_TN_preg(cpu_T[1], dc->op2);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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
	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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
	insn_len = dec_prep_alu_m(dc, 1, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 1, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
	gen_op_clrf(3);

	tcg_gen_mov_tl(cpu_T[0], cpu_T[1]);
	tcg_gen_movi_tl(cpu_T[1], 0);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 1, memsize);
	cris_cc_mask(dc, 0);
	crisv32_alu_op(dc, CC_OP_ADD, R_ACR, 4);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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
	insn_len = dec_prep_alu_m(dc, 0, 4);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZVC);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, CC_MASK_NZ);
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

	insn_len = dec_prep_alu_m(dc, 0, memsize);
	cris_cc_mask(dc, 0);
	if (dc->op2 == PR_CCS) {
		cris_evaluate_flags(dc);
		if (dc->user) {
			/* User space is not allowed to touch all flags.  */
			tcg_gen_andi_tl(cpu_T[1], cpu_T[1], 0x39f);
			tcg_gen_andi_tl(cpu_T[0], cpu_PR[PR_CCS], ~0x39f);
			tcg_gen_or_tl(cpu_T[1], cpu_T[0], cpu_T[1]);
		}
	}

	t_gen_mov_preg_TN(dc->op2, cpu_T[1]);

	do_postinc(dc, memsize);
	return insn_len;
}

static unsigned int dec_move_pm(DisasContext *dc)
{
	int memsize;

	memsize = preg_sizes[dc->op2];

	DIS(fprintf (logfile, "move.%c $p%u, [$r%u%s\n",
		     memsize_char(memsize), 
		     dc->op2, dc->op1, dc->postinc ? "+]" : "]"));

	/* prepare store. Address in T0, value in T1.  */
	t_gen_mov_TN_preg(cpu_T[1], dc->op2);
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	gen_store_T0_T1(dc, memsize);
	cris_cc_mask(dc, 0);
	if (dc->postinc)
	{
		tcg_gen_addi_tl(cpu_T[0], cpu_T[0], memsize);
		t_gen_mov_reg_TN(dc->op1, cpu_T[0]);
	}
	return 2;
}

static unsigned int dec_movem_mr(DisasContext *dc)
{
	int i;

	DIS(fprintf (logfile, "movem [$r%u%s, $r%u\n", dc->op1,
		    dc->postinc ? "+]" : "]", dc->op2));

	/* fetch the address into T0 and T1.  */
	t_gen_mov_TN_reg(cpu_T[1], dc->op1);
	for (i = 0; i <= dc->op2; i++) {
		/* Perform the load onto regnum i. Always dword wide.  */
		tcg_gen_mov_tl(cpu_T[0], cpu_T[1]);
		gen_load(dc, cpu_R[i], cpu_T[1], 4, 0);
		tcg_gen_addi_tl(cpu_T[1], cpu_T[1], 4);
	}
	/* writeback the updated pointer value.  */
	if (dc->postinc)
		t_gen_mov_reg_TN(dc->op1, cpu_T[1]);

	/* gen_load might want to evaluate the previous insns flags.  */
	cris_cc_mask(dc, 0);
	return 2;
}

static unsigned int dec_movem_rm(DisasContext *dc)
{
	int i;

	DIS(fprintf (logfile, "movem $r%u, [$r%u%s\n", dc->op2, dc->op1,
		     dc->postinc ? "+]" : "]"));

	for (i = 0; i <= dc->op2; i++) {
		/* Fetch register i into T1.  */
		t_gen_mov_TN_reg(cpu_T[1], i);
		/* Fetch the address into T0.  */
		t_gen_mov_TN_reg(cpu_T[0], dc->op1);
		/* Displace it.  */
		tcg_gen_addi_tl(cpu_T[0], cpu_T[0], i * 4);
		/* Perform the store.  */
		gen_store_T0_T1(dc, 4);
	}
	if (dc->postinc) {
		/* T0 should point to the last written addr, advance one more
		   step. */
		tcg_gen_addi_tl(cpu_T[0], cpu_T[0], 4);
		/* writeback the updated pointer value.  */
		t_gen_mov_reg_TN(dc->op1, cpu_T[0]);
	}
	cris_cc_mask(dc, 0);
	return 2;
}

static unsigned int dec_move_rm(DisasContext *dc)
{
	int memsize;

	memsize = memsize_zz(dc);

	DIS(fprintf (logfile, "move.%d $r%u, [$r%u]\n",
		     memsize, dc->op2, dc->op1));

	/* prepare store.  */
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	t_gen_mov_TN_reg(cpu_T[1], dc->op2);
	gen_store_T0_T1(dc, memsize);
	if (dc->postinc)
	{
		tcg_gen_addi_tl(cpu_T[0], cpu_T[0], memsize);
		t_gen_mov_reg_TN(dc->op1, cpu_T[0]);
	}
	cris_cc_mask(dc, 0);
	return 2;
}

static unsigned int dec_lapcq(DisasContext *dc)
{
	DIS(fprintf (logfile, "lapcq %x, $r%u\n",
		    dc->pc + dc->op1*2, dc->op2));
	cris_cc_mask(dc, 0);
	tcg_gen_movi_tl(cpu_T[1], dc->pc + dc->op1 * 2);
	crisv32_alu_op(dc, CC_OP_MOVE, dc->op2, 4);
	return 2;
}

static unsigned int dec_lapc_im(DisasContext *dc)
{
	unsigned int rd;
	int32_t imm;
	int32_t pc;

	rd = dc->op2;

	cris_cc_mask(dc, 0);
	imm = ldl_code(dc->pc + 2);
	DIS(fprintf (logfile, "lapc 0x%x, $r%u\n", imm + dc->pc, dc->op2));

	pc = dc->pc;
	pc += imm;
	t_gen_mov_reg_TN(rd, tcg_const_tl(pc));
	return 6;
}

/* Jump to special reg.  */
static unsigned int dec_jump_p(DisasContext *dc)
{
	DIS(fprintf (logfile, "jump $p%u\n", dc->op2));
	cris_cc_mask(dc, 0);

	t_gen_mov_TN_preg(cpu_T[0], dc->op2);
	/* rete will often have low bit set to indicate delayslot.  */
	tcg_gen_andi_tl(env_btarget, cpu_T[0], ~1);
	cris_prepare_dyn_jmp(dc);
	return 2;
}

/* Jump and save.  */
static unsigned int dec_jas_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "jas $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	tcg_gen_mov_tl(env_btarget, cpu_R[dc->op1]);
	if (dc->op2 > 15)
		abort();
	tcg_gen_movi_tl(cpu_PR[dc->op2], dc->pc + 4);

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
	tcg_gen_movi_tl(env_btarget, imm);
	t_gen_mov_preg_TN(dc->op2, tcg_const_tl(dc->pc + 8));
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
	tcg_gen_movi_tl(cpu_T[0], imm);
	t_gen_mov_env_TN(btarget, cpu_T[0]);
	tcg_gen_movi_tl(cpu_T[0], dc->pc + 8 + 4);
	t_gen_mov_preg_TN(dc->op2, cpu_T[0]);
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_jasc_r(DisasContext *dc)
{
	DIS(fprintf (logfile, "jasc_r $r%u, $p%u\n", dc->op1, dc->op2));
	cris_cc_mask(dc, 0);
	/* Stor the return address in Pd.  */
	t_gen_mov_TN_reg(cpu_T[0], dc->op1);
	t_gen_mov_env_TN(btarget, cpu_T[0]);
	tcg_gen_movi_tl(cpu_T[0], dc->pc + 4 + 4);
	t_gen_mov_preg_TN(dc->op2, cpu_T[0]);
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
	tcg_gen_movi_tl(cpu_T[0], dc->pc + simm);
	t_gen_mov_env_TN(btarget, cpu_T[0]);
	tcg_gen_movi_tl(cpu_T[0], dc->pc + 8);
	t_gen_mov_preg_TN(dc->op2, cpu_T[0]);
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
	tcg_gen_movi_tl(cpu_T[0], dc->pc + simm);
	t_gen_mov_env_TN(btarget, cpu_T[0]);
	tcg_gen_movi_tl(cpu_T[0], dc->pc + 12);
	t_gen_mov_preg_TN(dc->op2, cpu_T[0]);
	cris_prepare_dyn_jmp(dc);
	return 6;
}

static unsigned int dec_rfe_etc(DisasContext *dc)
{
	DIS(fprintf (logfile, "rfe_etc opc=%x pc=0x%x op1=%d op2=%d\n",
		    dc->opcode, dc->pc, dc->op1, dc->op2));

	cris_cc_mask(dc, 0);

	if (dc->op2 == 15) /* ignore halt.  */
		return 2;

	switch (dc->op2 & 7) {
		case 2:
			/* rfe.  */
			cris_evaluate_flags(dc);
			gen_op_ccs_rshift();
			/* FIXME: don't set the P-FLAG if R is set.  */
			tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], P_FLAG);
			/* Debug helper.  */
			tcg_gen_helper_0_0(helper_rfe);
			dc->is_jmp = DISAS_UPDATE;
			break;
		case 5:
			/* rfn.  */
			BUG();
			break;
		case 6:
			/* break.  */
			tcg_gen_movi_tl(cpu_T[0], dc->pc);
			t_gen_mov_env_TN(pc, cpu_T[0]);
			/* Breaks start at 16 in the exception vector.  */
			gen_op_break_im(dc->op1 + 16);
			dc->is_jmp = DISAS_UPDATE;
			break;
		default:
			printf ("op2=%x\n", dc->op2);
			BUG();
			break;

	}
	return 2;
}

static unsigned int dec_ftag_fidx_d_m(DisasContext *dc)
{
	/* Ignore D-cache flushes.  */
	return 2;
}

static unsigned int dec_ftag_fidx_i_m(DisasContext *dc)
{
	/* Ignore I-cache flushes.  */
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

	{DEC_FTAG_FIDX_I_M, dec_ftag_fidx_i_m},
	{DEC_FTAG_FIDX_D_M, dec_ftag_fidx_d_m},

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
				tcg_gen_movi_tl(cpu_T[0], dc->pc);
				t_gen_mov_env_TN(pc, cpu_T[0]);
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

	if (!logfile)
		logfile = stderr;

	if (tb->pc & 1)
		cpu_abort(env, "unaligned pc=%x erp=%x\n",
			  env->pc, env->pregs[PR_ERP]);
	pc_start = tb->pc;
	dc->env = env;
	dc->tb = tb;

	gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

	dc->is_jmp = DISAS_NEXT;
	dc->ppc = pc_start;
	dc->pc = pc_start;
	dc->singlestep_enabled = env->singlestep_enabled;
	dc->flags_live = 1;
	dc->flagx_live = 0;
	dc->flags_x = 0;
	dc->cc_mask = 0;
	cris_update_cc_op(dc, CC_OP_FLAGS, 4);

	dc->user = env->pregs[PR_CCS] & U_FLAG;
	dc->delayed_branch = 0;

	if (loglevel & CPU_LOG_TB_IN_ASM) {
		fprintf(logfile,
			"search=%d pc=%x ccs=%x pid=%x usp=%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n",
			search_pc, env->pc, env->pregs[PR_CCS], 
			env->pregs[PR_PID], env->pregs[PR_USP],
			env->regs[0], env->regs[1], env->regs[2], env->regs[3],
			env->regs[4], env->regs[5], env->regs[6], env->regs[7],
			env->regs[8], env->regs[9],
			env->regs[10], env->regs[11],
			env->regs[12], env->regs[13],
			env->regs[14], env->regs[15]);
		
	}

	next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
	lj = -1;
	do
	{
		check_breakpoint(env, dc);
		if (dc->is_jmp == DISAS_JUMP
		    || dc->is_jmp == DISAS_SWI)
			goto done;

		if (search_pc) {
			j = gen_opc_ptr - gen_opc_buf;
			if (lj < j) {
				lj++;
				while (lj < j)
					gen_opc_instr_start[lj++] = 0;
			}
			if (dc->delayed_branch == 1) {
				gen_opc_pc[lj] = dc->ppc | 1;
				gen_opc_instr_start[lj] = 0;
			}
			else {
				gen_opc_pc[lj] = dc->pc;
				gen_opc_instr_start[lj] = 1;
			}
		}

		dc->clear_x = 1;
		insn_len = cris_decoder(dc);
		STATS(gen_op_exec_insn());
		dc->ppc = dc->pc;
		dc->pc += insn_len;
		if (dc->clear_x)
			cris_clear_x_flag(dc);

		/* Check for delayed branches here. If we do it before
		   actually genereating any host code, the simulator will just
		   loop doing nothing for on this program location.  */
		if (dc->delayed_branch) {
			dc->delayed_branch--;
			if (dc->delayed_branch == 0)
			{
				if (dc->bcc == CC_A) {
					gen_op_jmp1 ();
					dc->is_jmp = DISAS_JUMP;
				}
				else {
					/* Conditional jmp.  */
					gen_op_cc_jmp (dc->delayed_pc, dc->pc);
					dc->is_jmp = DISAS_JUMP;
				}
			}
		}

		if (env->singlestep_enabled)
			break;
	} while (!dc->is_jmp && gen_opc_ptr < gen_opc_end
		 && ((dc->pc < next_page_start) || dc->delayed_branch));

	if (dc->delayed_branch == 1) {
		/* Reexecute the last insn.  */
		dc->pc = dc->ppc;
	}

	if (!dc->is_jmp) {
		D(printf("!jmp pc=%x jmp=%d db=%d\n", dc->pc, 
			 dc->is_jmp, dc->delayed_branch));
		/* T0 and env_pc should hold the new pc.  */
		tcg_gen_movi_tl(cpu_T[0], dc->pc);
		tcg_gen_mov_tl(env_pc, cpu_T[0]);
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
				tcg_gen_exit_tb(0);
				break;
			case DISAS_SWI:
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
		fprintf(logfile, "\nisize=%d osize=%d\n", 
			dc->pc - pc_start, gen_opc_ptr - gen_opc_buf);
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
		    env->pc, env->pregs[PR_CCS], env->btaken, env->btarget,
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
	srs = env->pregs[PR_SRS];
	cpu_fprintf(f, "\nsupport function regs bank %x:\n", srs);
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

static void tcg_macro_func(TCGContext *s, int macro_id, const int *dead_args)
{
}

CPUCRISState *cpu_cris_init (const char *cpu_model)
{
	CPUCRISState *env;
	int i;

	env = qemu_mallocz(sizeof(CPUCRISState));
	if (!env)
		return NULL;
	cpu_exec_init(env);

	tcg_set_macro_func(&tcg_ctx, tcg_macro_func);
	cpu_env = tcg_global_reg_new(TCG_TYPE_PTR, TCG_AREG0, "env");
#if TARGET_LONG_BITS > HOST_LONG_BITS
	cpu_T[0] = tcg_global_mem_new(TCG_TYPE_TL, 
				      TCG_AREG0, offsetof(CPUState, t0), "T0");
	cpu_T[1] = tcg_global_mem_new(TCG_TYPE_TL,
				      TCG_AREG0, offsetof(CPUState, t1), "T1");
#else
	cpu_T[0] = tcg_global_reg_new(TCG_TYPE_TL, TCG_AREG1, "T0");
	cpu_T[1] = tcg_global_reg_new(TCG_TYPE_TL, TCG_AREG2, "T1");
#endif

	cc_src = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				    offsetof(CPUState, cc_src), "cc_src");
	cc_dest = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				     offsetof(CPUState, cc_dest), 
				     "cc_dest");
	cc_result = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				       offsetof(CPUState, cc_result), 
				       "cc_result");
	cc_op = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				   offsetof(CPUState, cc_op), "cc_op");
	cc_size = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				     offsetof(CPUState, cc_size), 
				     "cc_size");
	cc_mask = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				     offsetof(CPUState, cc_mask),
				     "cc_mask");

	env_pc = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				     offsetof(CPUState, pc),
				     "pc");
	env_btarget = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
				     offsetof(CPUState, btarget),
				     "btarget");

	for (i = 0; i < 16; i++) {
		cpu_R[i] = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
					      offsetof(CPUState, regs[i]), 
					      regnames[i]);
	}
	for (i = 0; i < 16; i++) {
		cpu_PR[i] = tcg_global_mem_new(TCG_TYPE_PTR, TCG_AREG0, 
					       offsetof(CPUState, pregs[i]), 
					       pregnames[i]);
	}

	TCG_HELPER(helper_tlb_update);
	TCG_HELPER(helper_tlb_flush);
	TCG_HELPER(helper_rfe);
	TCG_HELPER(helper_store);
	TCG_HELPER(helper_dump);
	TCG_HELPER(helper_dummy);

	TCG_HELPER(helper_evaluate_flags_muls);
	TCG_HELPER(helper_evaluate_flags_mulu);
	TCG_HELPER(helper_evaluate_flags_mcp);
	TCG_HELPER(helper_evaluate_flags_alu_4);
	TCG_HELPER(helper_evaluate_flags_move_4);
	TCG_HELPER(helper_evaluate_flags_move_2);
	TCG_HELPER(helper_evaluate_flags);

	cpu_reset(env);
	return env;
}

void cpu_reset (CPUCRISState *env)
{
	memset(env, 0, offsetof(CPUCRISState, breakpoints));
	tlb_flush(env, 1);

#if defined(CONFIG_USER_ONLY)
	/* start in user mode with interrupts enabled.  */
	env->pregs[PR_CCS] |= U_FLAG | I_FLAG;
#else
	env->pregs[PR_CCS] = 0;
#endif
}

void gen_pc_load(CPUState *env, struct TranslationBlock *tb,
                 unsigned long searched_pc, int pc_pos, void *puc)
{
    env->pc = gen_opc_pc[pc_pos];
}
