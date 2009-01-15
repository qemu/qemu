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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

/*
 * FIXME:
 * The condition code translation is in need of attention.
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

#define GEN_HELPER 1
#include "helper.h"

#define DISAS_CRIS 0
#if DISAS_CRIS
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
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

static TCGv_ptr cpu_env;
static TCGv cpu_R[16];
static TCGv cpu_PR[16];
static TCGv cc_x;
static TCGv cc_src;
static TCGv cc_dest;
static TCGv cc_result;
static TCGv cc_op;
static TCGv cc_size;
static TCGv cc_mask;

static TCGv env_btaken;
static TCGv env_btarget;
static TCGv env_pc;

#include "gen-icount.h"

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

	int cc_size_uptodate; /* -1 invalid or last written value.  */

	int cc_x_uptodate;  /* 1 - ccs, 2 - known | X_FLAG. 0 not uptodate.  */
	int flags_uptodate; /* Wether or not $ccs is uptodate.  */
	int flagx_known; /* Wether or not flags_x has the x flag known at
			    translation time.  */
	int flags_x;

	int clear_x; /* Clear x after this insn?  */
	int cpustate_changed;
	unsigned int tb_flags; /* tb dependent flags.  */
	int is_jmp;

#define JMP_NOJMP    0
#define JMP_DIRECT   1
#define JMP_INDIRECT 2
	int jmp; /* 0=nojmp, 1=direct, 2=indirect.  */ 
	uint32_t jmp_pc;

	int delayed_branch;

	struct TranslationBlock *tb;
	int singlestep_enabled;
} DisasContext;

static void gen_BUG(DisasContext *dc, const char *file, int line)
{
	printf ("BUG: pc=%x %s %d\n", dc->pc, file, line);
	qemu_log("BUG: pc=%x %s %d\n", dc->pc, file, line);
	cpu_abort(dc->env, "%s:%d\n", file, line);
}

static const char *regnames[] =
{
	"$r0", "$r1", "$r2", "$r3",
	"$r4", "$r5", "$r6", "$r7",
	"$r8", "$r9", "$r10", "$r11",
	"$r12", "$r13", "$sp", "$acr",
};
static const char *pregnames[] =
{
	"$bz", "$vr", "$pid", "$srs",
	"$wz", "$exs", "$eda", "$mof",
	"$dz", "$ebp", "$erp", "$srp",
	"$nrp", "$ccs", "$usp", "$spc",
};

/* We need this table to handle preg-moves with implicit width.  */
static int preg_sizes[] = {
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
	else if (r == PR_EDA) {
		printf("read from EDA!\n");
		tcg_gen_mov_tl(tn, cpu_PR[r]);
	}
	else
		tcg_gen_mov_tl(tn, cpu_PR[r]);
}
static inline void t_gen_mov_preg_TN(DisasContext *dc, int r, TCGv tn)
{
	if (r < 0 || r > 15)
		fprintf(stderr, "wrong register write $p%d\n", r);
	if (r == PR_BZ || r == PR_WZ || r == PR_DZ)
		return;
	else if (r == PR_SRS)
		tcg_gen_andi_tl(cpu_PR[r], tn, 3);
	else {
		if (r == PR_PID) 
			gen_helper_tlb_flush_pid(tn);
		if (dc->tb_flags & S_FLAG && r == PR_SPC) 
			gen_helper_spc_write(tn);
		else if (r == PR_CCS)
			dc->cpustate_changed = 1;
		tcg_gen_mov_tl(cpu_PR[r], tn);
	}
}

static inline void t_gen_raise_exception(uint32_t index)
{
        TCGv_i32 tmp = tcg_const_i32(index);
	gen_helper_raise_exception(tmp);
        tcg_temp_free_i32(tmp);
}

static void t_gen_lsl(TCGv d, TCGv a, TCGv b)
{
	TCGv t0, t_31;

	t0 = tcg_temp_new();
	t_31 = tcg_const_tl(31);
	tcg_gen_shl_tl(d, a, b);

	tcg_gen_sub_tl(t0, t_31, b);
	tcg_gen_sar_tl(t0, t0, t_31);
	tcg_gen_and_tl(t0, t0, d);
	tcg_gen_xor_tl(d, d, t0);
	tcg_temp_free(t0);
	tcg_temp_free(t_31);
}

static void t_gen_lsr(TCGv d, TCGv a, TCGv b)
{
	TCGv t0, t_31;

	t0 = tcg_temp_new();
	t_31 = tcg_temp_new();
	tcg_gen_shr_tl(d, a, b);

	tcg_gen_movi_tl(t_31, 31);
	tcg_gen_sub_tl(t0, t_31, b);
	tcg_gen_sar_tl(t0, t0, t_31);
	tcg_gen_and_tl(t0, t0, d);
	tcg_gen_xor_tl(d, d, t0);
	tcg_temp_free(t0);
	tcg_temp_free(t_31);
}

static void t_gen_asr(TCGv d, TCGv a, TCGv b)
{
	TCGv t0, t_31;

	t0 = tcg_temp_new();
	t_31 = tcg_temp_new();
	tcg_gen_sar_tl(d, a, b);

	tcg_gen_movi_tl(t_31, 31);
	tcg_gen_sub_tl(t0, t_31, b);
	tcg_gen_sar_tl(t0, t0, t_31);
	tcg_gen_or_tl(d, d, t0);
	tcg_temp_free(t0);
	tcg_temp_free(t_31);
}

/* 64-bit signed mul, lower result in d and upper in d2.  */
static void t_gen_muls(TCGv d, TCGv d2, TCGv a, TCGv b)
{
	TCGv_i64 t0, t1;

	t0 = tcg_temp_new_i64();
	t1 = tcg_temp_new_i64();

	tcg_gen_ext_i32_i64(t0, a);
	tcg_gen_ext_i32_i64(t1, b);
	tcg_gen_mul_i64(t0, t0, t1);

	tcg_gen_trunc_i64_i32(d, t0);
	tcg_gen_shri_i64(t0, t0, 32);
	tcg_gen_trunc_i64_i32(d2, t0);

	tcg_temp_free_i64(t0);
	tcg_temp_free_i64(t1);
}

/* 64-bit unsigned muls, lower result in d and upper in d2.  */
static void t_gen_mulu(TCGv d, TCGv d2, TCGv a, TCGv b)
{
	TCGv_i64 t0, t1;

	t0 = tcg_temp_new_i64();
	t1 = tcg_temp_new_i64();

	tcg_gen_extu_i32_i64(t0, a);
	tcg_gen_extu_i32_i64(t1, b);
	tcg_gen_mul_i64(t0, t0, t1);

	tcg_gen_trunc_i64_i32(d, t0);
	tcg_gen_shri_i64(t0, t0, 32);
	tcg_gen_trunc_i64_i32(d2, t0);

	tcg_temp_free_i64(t0);
	tcg_temp_free_i64(t1);
}

static void t_gen_cris_dstep(TCGv d, TCGv a, TCGv b)
{
	int l1;

	l1 = gen_new_label();

	/* 
	 * d <<= 1
	 * if (d >= s)
	 *    d -= s;
	 */
	tcg_gen_shli_tl(d, a, 1);
	tcg_gen_brcond_tl(TCG_COND_LTU, d, b, l1);
	tcg_gen_sub_tl(d, d, b);
	gen_set_label(l1);
}

/* Extended arithmetics on CRIS.  */
static inline void t_gen_add_flag(TCGv d, int flag)
{
	TCGv c;

	c = tcg_temp_new();
	t_gen_mov_TN_preg(c, PR_CCS);
	/* Propagate carry into d.  */
	tcg_gen_andi_tl(c, c, 1 << flag);
	if (flag)
		tcg_gen_shri_tl(c, c, flag);
	tcg_gen_add_tl(d, d, c);
	tcg_temp_free(c);
}

static inline void t_gen_addx_carry(DisasContext *dc, TCGv d)
{
	if (dc->flagx_known) {
		if (dc->flags_x) {
			TCGv c;
            
			c = tcg_temp_new();
			t_gen_mov_TN_preg(c, PR_CCS);
			/* C flag is already at bit 0.  */
			tcg_gen_andi_tl(c, c, C_FLAG);
			tcg_gen_add_tl(d, d, c);
			tcg_temp_free(c);
		}
	} else {
		TCGv x, c;

		x = tcg_temp_new();
		c = tcg_temp_new();
		t_gen_mov_TN_preg(x, PR_CCS);
		tcg_gen_mov_tl(c, x);

		/* Propagate carry into d if X is set. Branch free.  */
		tcg_gen_andi_tl(c, c, C_FLAG);
		tcg_gen_andi_tl(x, x, X_FLAG);
		tcg_gen_shri_tl(x, x, 4);

		tcg_gen_and_tl(x, x, c);
		tcg_gen_add_tl(d, d, x);        
		tcg_temp_free(x);
		tcg_temp_free(c);
	}
}

static inline void t_gen_subx_carry(DisasContext *dc, TCGv d)
{
	if (dc->flagx_known) {
		if (dc->flags_x) {
			TCGv c;
            
			c = tcg_temp_new();
			t_gen_mov_TN_preg(c, PR_CCS);
			/* C flag is already at bit 0.  */
			tcg_gen_andi_tl(c, c, C_FLAG);
			tcg_gen_sub_tl(d, d, c);
			tcg_temp_free(c);
		}
	} else {
		TCGv x, c;

		x = tcg_temp_new();
		c = tcg_temp_new();
		t_gen_mov_TN_preg(x, PR_CCS);
		tcg_gen_mov_tl(c, x);

		/* Propagate carry into d if X is set. Branch free.  */
		tcg_gen_andi_tl(c, c, C_FLAG);
		tcg_gen_andi_tl(x, x, X_FLAG);
		tcg_gen_shri_tl(x, x, 4);

		tcg_gen_and_tl(x, x, c);
		tcg_gen_sub_tl(d, d, x);
		tcg_temp_free(x);
		tcg_temp_free(c);
	}
}

/* Swap the two bytes within each half word of the s operand.
   T0 = ((T0 << 8) & 0xff00ff00) | ((T0 >> 8) & 0x00ff00ff)  */
static inline void t_gen_swapb(TCGv d, TCGv s)
{
	TCGv t, org_s;

	t = tcg_temp_new();
	org_s = tcg_temp_new();

	/* d and s may refer to the same object.  */
	tcg_gen_mov_tl(org_s, s);
	tcg_gen_shli_tl(t, org_s, 8);
	tcg_gen_andi_tl(d, t, 0xff00ff00);
	tcg_gen_shri_tl(t, org_s, 8);
	tcg_gen_andi_tl(t, t, 0x00ff00ff);
	tcg_gen_or_tl(d, d, t);
	tcg_temp_free(t);
	tcg_temp_free(org_s);
}

/* Swap the halfwords of the s operand.  */
static inline void t_gen_swapw(TCGv d, TCGv s)
{
	TCGv t;
	/* d and s refer the same object.  */
	t = tcg_temp_new();
	tcg_gen_mov_tl(t, s);
	tcg_gen_shli_tl(d, t, 16);
	tcg_gen_shri_tl(t, t, 16);
	tcg_gen_or_tl(d, d, t);
	tcg_temp_free(t);
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
	t = tcg_temp_new();
	org_s = tcg_temp_new();
	tcg_gen_mov_tl(org_s, s);

	tcg_gen_shli_tl(t, org_s,  bitrev[0].shift);
	tcg_gen_andi_tl(d, t,  bitrev[0].mask);
	for (i = 1; i < ARRAY_SIZE(bitrev); i++) {
		if (bitrev[i].shift >= 0) {
			tcg_gen_shli_tl(t, org_s,  bitrev[i].shift);
		} else {
			tcg_gen_shri_tl(t, org_s,  -bitrev[i].shift);
		}
		tcg_gen_andi_tl(t, t,  bitrev[i].mask);
		tcg_gen_or_tl(d, d, t);
	}
	tcg_temp_free(t);
	tcg_temp_free(org_s);
}

static void t_gen_cc_jmp(TCGv pc_true, TCGv pc_false)
{
	TCGv btaken;
	int l1;

	l1 = gen_new_label();
	btaken = tcg_temp_new();

	/* Conditional jmp.  */
	tcg_gen_mov_tl(btaken, env_btaken);
	tcg_gen_mov_tl(env_pc, pc_false);
	tcg_gen_brcondi_tl(TCG_COND_EQ, btaken, 0, l1);
	tcg_gen_mov_tl(env_pc, pc_true);
	gen_set_label(l1);

	tcg_temp_free(btaken);
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
		tcg_gen_movi_tl(env_pc, dest);
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
	if (dc->flagx_known && dc->flags_x)
		dc->flags_uptodate = 0;

	dc->flagx_known = 1;
	dc->flags_x = 0;
}

static void cris_flush_cc_state(DisasContext *dc)
{
	if (dc->cc_size_uptodate != dc->cc_size) {
		tcg_gen_movi_tl(cc_size, dc->cc_size);
		dc->cc_size_uptodate = dc->cc_size;
	}
	tcg_gen_movi_tl(cc_op, dc->cc_op);
	tcg_gen_movi_tl(cc_mask, dc->cc_mask);
}

static void cris_evaluate_flags(DisasContext *dc)
{
	if (dc->flags_uptodate)
		return;

	cris_flush_cc_state(dc);

	switch (dc->cc_op)
	{
	case CC_OP_MCP:
		gen_helper_evaluate_flags_mcp(cpu_PR[PR_CCS],
					cpu_PR[PR_CCS], cc_src,
					cc_dest, cc_result);
		break;
	case CC_OP_MULS:
		gen_helper_evaluate_flags_muls(cpu_PR[PR_CCS],
					cpu_PR[PR_CCS], cc_result,
					cpu_PR[PR_MOF]);
		break;
	case CC_OP_MULU:
		gen_helper_evaluate_flags_mulu(cpu_PR[PR_CCS],
					cpu_PR[PR_CCS], cc_result,
					cpu_PR[PR_MOF]);
		break;
	case CC_OP_MOVE:
	case CC_OP_AND:
	case CC_OP_OR:
	case CC_OP_XOR:
	case CC_OP_ASR:
	case CC_OP_LSR:
	case CC_OP_LSL:
		switch (dc->cc_size)
		{
		case 4:
			gen_helper_evaluate_flags_move_4(cpu_PR[PR_CCS],
						cpu_PR[PR_CCS], cc_result);
			break;
		case 2:
			gen_helper_evaluate_flags_move_2(cpu_PR[PR_CCS],
						cpu_PR[PR_CCS], cc_result);
			break;
		default:
			gen_helper_evaluate_flags();
			break;
		}
		break;
	case CC_OP_FLAGS:
		/* live.  */
		break;
	case CC_OP_SUB:
	case CC_OP_CMP:
		if (dc->cc_size == 4)
			gen_helper_evaluate_flags_sub_4(cpu_PR[PR_CCS],
				cpu_PR[PR_CCS], cc_src, cc_dest, cc_result);
		else
			gen_helper_evaluate_flags();

		break;
	default:
		switch (dc->cc_size)
		{
			case 4:
			gen_helper_evaluate_flags_alu_4(cpu_PR[PR_CCS],
				cpu_PR[PR_CCS], cc_src, cc_dest, cc_result);
				break;
			default:
				gen_helper_evaluate_flags();
				break;
		}
		break;
	}

	if (dc->flagx_known) {
		if (dc->flags_x)
			tcg_gen_ori_tl(cpu_PR[PR_CCS], 
				       cpu_PR[PR_CCS], X_FLAG);
		else
			tcg_gen_andi_tl(cpu_PR[PR_CCS], 
					cpu_PR[PR_CCS], ~X_FLAG);
        }
	dc->flags_uptodate = 1;
}

static void cris_cc_mask(DisasContext *dc, unsigned int mask)
{
	uint32_t ovl;

	if (!mask) {
		dc->update_cc = 0;
		return;
	}	

	/* Check if we need to evaluate the condition codes due to 
	   CC overlaying.  */
	ovl = (dc->cc_mask ^ mask) & ~mask;
	if (ovl) {
		/* TODO: optimize this case. It trigs all the time.  */
		cris_evaluate_flags (dc);
	}
	dc->cc_mask = mask;
	dc->update_cc = 1;
}

static void cris_update_cc_op(DisasContext *dc, int op, int size)
{
	dc->cc_op = op;
	dc->cc_size = size;
	dc->flags_uptodate = 0;
}

static inline void cris_update_cc_x(DisasContext *dc)
{
	/* Save the x flag state at the time of the cc snapshot.  */
	if (dc->flagx_known) {
		if (dc->cc_x_uptodate == (2 | dc->flags_x))
			return;
		tcg_gen_movi_tl(cc_x, dc->flags_x);
		dc->cc_x_uptodate = 2 | dc->flags_x;
	}
	else {
		tcg_gen_andi_tl(cc_x, cpu_PR[PR_CCS], X_FLAG);
		dc->cc_x_uptodate = 1;
	}
}

/* Update cc prior to executing ALU op. Needs source operands untouched.  */
static void cris_pre_alu_update_cc(DisasContext *dc, int op, 
				   TCGv dst, TCGv src, int size)
{
	if (dc->update_cc) {
		cris_update_cc_op(dc, op, size);
		tcg_gen_mov_tl(cc_src, src);

		if (op != CC_OP_MOVE
		    && op != CC_OP_AND
		    && op != CC_OP_OR
		    && op != CC_OP_XOR
		    && op != CC_OP_ASR
		    && op != CC_OP_LSR
		    && op != CC_OP_LSL)
			tcg_gen_mov_tl(cc_dest, dst);

		cris_update_cc_x(dc);
	}
}

/* Update cc after executing ALU op. needs the result.  */
static inline void cris_update_result(DisasContext *dc, TCGv res)
{
	if (dc->update_cc)
		tcg_gen_mov_tl(cc_result, res);
}

/* Returns one if the write back stage should execute.  */
static void cris_alu_op_exec(DisasContext *dc, int op, 
			       TCGv dst, TCGv a, TCGv b, int size)
{
	/* Emit the ALU insns.  */
	switch (op)
	{
		case CC_OP_ADD:
			tcg_gen_add_tl(dst, a, b);
			/* Extended arithmetics.  */
			t_gen_addx_carry(dc, dst);
			break;
		case CC_OP_ADDC:
			tcg_gen_add_tl(dst, a, b);
			t_gen_add_flag(dst, 0); /* C_FLAG.  */
			break;
		case CC_OP_MCP:
			tcg_gen_add_tl(dst, a, b);
			t_gen_add_flag(dst, 8); /* R_FLAG.  */
			break;
		case CC_OP_SUB:
			tcg_gen_sub_tl(dst, a, b);
			/* Extended arithmetics.  */
			t_gen_subx_carry(dc, dst);
			break;
		case CC_OP_MOVE:
			tcg_gen_mov_tl(dst, b);
			break;
		case CC_OP_OR:
			tcg_gen_or_tl(dst, a, b);
			break;
		case CC_OP_AND:
			tcg_gen_and_tl(dst, a, b);
			break;
		case CC_OP_XOR:
			tcg_gen_xor_tl(dst, a, b);
			break;
		case CC_OP_LSL:
			t_gen_lsl(dst, a, b);
			break;
		case CC_OP_LSR:
			t_gen_lsr(dst, a, b);
			break;
		case CC_OP_ASR:
			t_gen_asr(dst, a, b);
			break;
		case CC_OP_NEG:
			tcg_gen_neg_tl(dst, b);
			/* Extended arithmetics.  */
			t_gen_subx_carry(dc, dst);
			break;
		case CC_OP_LZ:
			gen_helper_lz(dst, b);
			break;
		case CC_OP_MULS:
			t_gen_muls(dst, cpu_PR[PR_MOF], a, b);
			break;
		case CC_OP_MULU:
			t_gen_mulu(dst, cpu_PR[PR_MOF], a, b);
			break;
		case CC_OP_DSTEP:
			t_gen_cris_dstep(dst, a, b);
			break;
		case CC_OP_BOUND:
		{
			int l1;
			l1 = gen_new_label();
			tcg_gen_mov_tl(dst, a);
			tcg_gen_brcond_tl(TCG_COND_LEU, a, b, l1);
			tcg_gen_mov_tl(dst, b);
			gen_set_label(l1);
		}
		break;
		case CC_OP_CMP:
			tcg_gen_sub_tl(dst, a, b);
			/* Extended arithmetics.  */
			t_gen_subx_carry(dc, dst);
			break;
		default:
			qemu_log("illegal ALU op.\n");
			BUG();
			break;
	}

	if (size == 1)
		tcg_gen_andi_tl(dst, dst, 0xff);
	else if (size == 2)
		tcg_gen_andi_tl(dst, dst, 0xffff);
}

static void cris_alu(DisasContext *dc, int op,
			       TCGv d, TCGv op_a, TCGv op_b, int size)
{
	TCGv tmp;
	int writeback;

	writeback = 1;

	if (op == CC_OP_CMP) {
		tmp = tcg_temp_new();
		writeback = 0;
	} else if (size == 4) {
		tmp = d;
		writeback = 0;
	} else
		tmp = tcg_temp_new();


	cris_pre_alu_update_cc(dc, op, op_a, op_b, size);
	cris_alu_op_exec(dc, op, tmp, op_a, op_b, size);
	cris_update_result(dc, tmp);

	/* Writeback.  */
	if (writeback) {
		if (size == 1)
			tcg_gen_andi_tl(d, d, ~0xff);
		else
			tcg_gen_andi_tl(d, d, ~0xffff);
		tcg_gen_or_tl(d, d, tmp);
	}
	if (!TCGV_EQUAL(tmp, d))
		tcg_temp_free(tmp);
}

static int arith_cc(DisasContext *dc)
{
	if (dc->update_cc) {
		switch (dc->cc_op) {
			case CC_OP_ADDC: return 1;
			case CC_OP_ADD: return 1;
			case CC_OP_SUB: return 1;
			case CC_OP_DSTEP: return 1;
			case CC_OP_LSL: return 1;
			case CC_OP_LSR: return 1;
			case CC_OP_ASR: return 1;
			case CC_OP_CMP: return 1;
			case CC_OP_NEG: return 1;
			case CC_OP_OR: return 1;
			case CC_OP_AND: return 1;
			case CC_OP_XOR: return 1;
			case CC_OP_MULU: return 1;
			case CC_OP_MULS: return 1;
			default:
				return 0;
		}
	}
	return 0;
}

static void gen_tst_cc (DisasContext *dc, TCGv cc, int cond)
{
	int arith_opt, move_opt;

	/* TODO: optimize more condition codes.  */

	/*
	 * If the flags are live, we've gotta look into the bits of CCS.
	 * Otherwise, if we just did an arithmetic operation we try to
	 * evaluate the condition code faster.
	 *
	 * When this function is done, T0 should be non-zero if the condition
	 * code is true.
	 */
	arith_opt = arith_cc(dc) && !dc->flags_uptodate;
	move_opt = (dc->cc_op == CC_OP_MOVE);
	switch (cond) {
		case CC_EQ:
			if (arith_opt || move_opt) {
				/* If cc_result is zero, T0 should be 
				   non-zero otherwise T0 should be zero.  */
				int l1;
				l1 = gen_new_label();
				tcg_gen_movi_tl(cc, 0);
				tcg_gen_brcondi_tl(TCG_COND_NE, cc_result, 
						   0, l1);
				tcg_gen_movi_tl(cc, 1);
				gen_set_label(l1);
			}
			else {
				cris_evaluate_flags(dc);
				tcg_gen_andi_tl(cc, 
						cpu_PR[PR_CCS], Z_FLAG);
			}
			break;
		case CC_NE:
			if (arith_opt || move_opt)
				tcg_gen_mov_tl(cc, cc_result);
			else {
				cris_evaluate_flags(dc);
				tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
						Z_FLAG);
				tcg_gen_andi_tl(cc, cc, Z_FLAG);
			}
			break;
		case CC_CS:
			cris_evaluate_flags(dc);
			tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], C_FLAG);
			break;
		case CC_CC:
			cris_evaluate_flags(dc);
			tcg_gen_xori_tl(cc, cpu_PR[PR_CCS], C_FLAG);
			tcg_gen_andi_tl(cc, cc, C_FLAG);
			break;
		case CC_VS:
			cris_evaluate_flags(dc);
			tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], V_FLAG);
			break;
		case CC_VC:
			cris_evaluate_flags(dc);
			tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
					V_FLAG);
			tcg_gen_andi_tl(cc, cc, V_FLAG);
			break;
		case CC_PL:
			if (arith_opt || move_opt) {
				int bits = 31;

				if (dc->cc_size == 1)
					bits = 7;
				else if (dc->cc_size == 2)
					bits = 15;	

				tcg_gen_shri_tl(cc, cc_result, bits);
				tcg_gen_xori_tl(cc, cc, 1);
			} else {
				cris_evaluate_flags(dc);
				tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
						N_FLAG);
				tcg_gen_andi_tl(cc, cc, N_FLAG);
			}
			break;
		case CC_MI:
			if (arith_opt || move_opt) {
				int bits = 31;

				if (dc->cc_size == 1)
					bits = 7;
				else if (dc->cc_size == 2)
					bits = 15;	

				tcg_gen_shri_tl(cc, cc_result, 31);
			}
			else {
				cris_evaluate_flags(dc);
				tcg_gen_andi_tl(cc, cpu_PR[PR_CCS],
						N_FLAG);
			}
			break;
		case CC_LS:
			cris_evaluate_flags(dc);
			tcg_gen_andi_tl(cc, cpu_PR[PR_CCS],
					C_FLAG | Z_FLAG);
			break;
		case CC_HI:
			cris_evaluate_flags(dc);
			{
				TCGv tmp;

				tmp = tcg_temp_new();
				tcg_gen_xori_tl(tmp, cpu_PR[PR_CCS],
						C_FLAG | Z_FLAG);
				/* Overlay the C flag on top of the Z.  */
				tcg_gen_shli_tl(cc, tmp, 2);
				tcg_gen_and_tl(cc, tmp, cc);
				tcg_gen_andi_tl(cc, cc, Z_FLAG);

				tcg_temp_free(tmp);
			}
			break;
		case CC_GE:
			cris_evaluate_flags(dc);
			/* Overlay the V flag on top of the N.  */
			tcg_gen_shli_tl(cc, cpu_PR[PR_CCS], 2);
			tcg_gen_xor_tl(cc,
				       cpu_PR[PR_CCS], cc);
			tcg_gen_andi_tl(cc, cc, N_FLAG);
			tcg_gen_xori_tl(cc, cc, N_FLAG);
			break;
		case CC_LT:
			cris_evaluate_flags(dc);
			/* Overlay the V flag on top of the N.  */
			tcg_gen_shli_tl(cc, cpu_PR[PR_CCS], 2);
			tcg_gen_xor_tl(cc,
				       cpu_PR[PR_CCS], cc);
			tcg_gen_andi_tl(cc, cc, N_FLAG);
			break;
		case CC_GT:
			cris_evaluate_flags(dc);
			{
				TCGv n, z;

				n = tcg_temp_new();
				z = tcg_temp_new();

				/* To avoid a shift we overlay everything on
				   the V flag.  */
				tcg_gen_shri_tl(n, cpu_PR[PR_CCS], 2);
				tcg_gen_shri_tl(z, cpu_PR[PR_CCS], 1);
				/* invert Z.  */
				tcg_gen_xori_tl(z, z, 2);

				tcg_gen_xor_tl(n, n, cpu_PR[PR_CCS]);
				tcg_gen_xori_tl(n, n, 2);
				tcg_gen_and_tl(cc, z, n);
				tcg_gen_andi_tl(cc, cc, 2);

				tcg_temp_free(n);
				tcg_temp_free(z);
			}
			break;
		case CC_LE:
			cris_evaluate_flags(dc);
			{
				TCGv n, z;

				n = tcg_temp_new();
				z = tcg_temp_new();

				/* To avoid a shift we overlay everything on
				   the V flag.  */
				tcg_gen_shri_tl(n, cpu_PR[PR_CCS], 2);
				tcg_gen_shri_tl(z, cpu_PR[PR_CCS], 1);

				tcg_gen_xor_tl(n, n, cpu_PR[PR_CCS]);
				tcg_gen_or_tl(cc, z, n);
				tcg_gen_andi_tl(cc, cc, 2);

				tcg_temp_free(n);
				tcg_temp_free(z);
			}
			break;
		case CC_P:
			cris_evaluate_flags(dc);
			tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], P_FLAG);
			break;
		case CC_A:
			tcg_gen_movi_tl(cc, 1);
			break;
		default:
			BUG();
			break;
	};
}

static void cris_store_direct_jmp(DisasContext *dc)
{
	/* Store the direct jmp state into the cpu-state.  */
	if (dc->jmp == JMP_DIRECT) {
		tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
		tcg_gen_movi_tl(env_btaken, 1);
	}
}

static void cris_prepare_cc_branch (DisasContext *dc, 
				    int offset, int cond)
{
	/* This helps us re-schedule the micro-code to insns in delay-slots
	   before the actual jump.  */
	dc->delayed_branch = 2;
	dc->jmp_pc = dc->pc + offset;

	if (cond != CC_A)
	{
		dc->jmp = JMP_INDIRECT;
		gen_tst_cc (dc, env_btaken, cond);
		tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
	} else {
		/* Allow chaining.  */
		dc->jmp = JMP_DIRECT;
	}
}


/* jumps, when the dest is in a live reg for example. Direct should be set
   when the dest addr is constant to allow tb chaining.  */
static inline void cris_prepare_jmp (DisasContext *dc, unsigned int type)
{
	/* This helps us re-schedule the micro-code to insns in delay-slots
	   before the actual jump.  */
	dc->delayed_branch = 2;
	dc->jmp = type;
	if (type == JMP_INDIRECT)
		tcg_gen_movi_tl(env_btaken, 1);
}

static void gen_load64(DisasContext *dc, TCGv_i64 dst, TCGv addr)
{
	int mem_index = cpu_mmu_index(dc->env);

	/* If we get a fault on a delayslot we must keep the jmp state in
	   the cpu-state to be able to re-execute the jmp.  */
	if (dc->delayed_branch == 1)
		cris_store_direct_jmp(dc);

        tcg_gen_qemu_ld64(dst, addr, mem_index);
}

static void gen_load(DisasContext *dc, TCGv dst, TCGv addr, 
		     unsigned int size, int sign)
{
	int mem_index = cpu_mmu_index(dc->env);

	/* If we get a fault on a delayslot we must keep the jmp state in
	   the cpu-state to be able to re-execute the jmp.  */
	if (dc->delayed_branch == 1)
		cris_store_direct_jmp(dc);

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
	else if (size == 4) {
		tcg_gen_qemu_ld32u(dst, addr, mem_index);
	}
	else {
		abort();
	}
}

static void gen_store (DisasContext *dc, TCGv addr, TCGv val,
		       unsigned int size)
{
	int mem_index = cpu_mmu_index(dc->env);

	/* If we get a fault on a delayslot we must keep the jmp state in
	   the cpu-state to be able to re-execute the jmp.  */
	if (dc->delayed_branch == 1)
 		cris_store_direct_jmp(dc);


	/* Conditional writes. We only support the kind were X and P are known
	   at translation time.  */
	if (dc->flagx_known && dc->flags_x && (dc->tb_flags & P_FLAG)) {
		dc->postinc = 0;
		cris_evaluate_flags(dc);
		tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], C_FLAG);
		return;
	}

	if (size == 1)
		tcg_gen_qemu_st8(val, addr, mem_index);
	else if (size == 2)
		tcg_gen_qemu_st16(val, addr, mem_index);
	else
		tcg_gen_qemu_st32(val, addr, mem_index);

	if (dc->flagx_known && dc->flags_x) {
		cris_evaluate_flags(dc);
		tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~C_FLAG);
	}
}

static inline void t_gen_sext(TCGv d, TCGv s, int size)
{
	if (size == 1)
		tcg_gen_ext8s_i32(d, s);
	else if (size == 2)
		tcg_gen_ext16s_i32(d, s);
	else if(!TCGV_EQUAL(d, s))
		tcg_gen_mov_tl(d, s);
}

static inline void t_gen_zext(TCGv d, TCGv s, int size)
{
	if (size == 1)
		tcg_gen_ext8u_i32(d, s);
	else if (size == 2)
		tcg_gen_ext16u_i32(d, s);
	else if (!TCGV_EQUAL(d, s))
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

static inline unsigned int memsize_z(DisasContext *dc)
{
	return dc->zsize + 1;
}

static inline unsigned int memsize_zz(DisasContext *dc)
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

static inline void dec_prep_move_r(DisasContext *dc, int rs, int rd,
				   int size, int s_ext, TCGv dst)
{
	if (s_ext)
		t_gen_sext(dst, cpu_R[rs], size);
	else
		t_gen_zext(dst, cpu_R[rs], size);
}

/* Prepare T0 and T1 for a register alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static void dec_prep_alu_r(DisasContext *dc, int rs, int rd,
			  int size, int s_ext, TCGv dst, TCGv src)
{
	dec_prep_move_r(dc, rs, rd, size, s_ext, src);

	if (s_ext)
		t_gen_sext(dst, cpu_R[rd], size);
	else
		t_gen_zext(dst, cpu_R[rd], size);
}

static int dec_prep_move_m(DisasContext *dc, int s_ext, int memsize,
			   TCGv dst)
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

		if (memsize != 4) {
			if (s_ext) {
				if (memsize == 1)
					imm = ldsb_code(dc->pc + 2);
				else
					imm = ldsw_code(dc->pc + 2);
			} else {
				if (memsize == 1)
					imm = ldub_code(dc->pc + 2);
				else
					imm = lduw_code(dc->pc + 2);
			}
		} else
			imm = ldl_code(dc->pc + 2);
			
		tcg_gen_movi_tl(dst, imm);
		dc->postinc = 0;
	} else {
		cris_flush_cc_state(dc);
		gen_load(dc, dst, cpu_R[rs], memsize, 0);
		if (s_ext)
			t_gen_sext(dst, dst, memsize);
		else
			t_gen_zext(dst, dst, memsize);
	}
	return insn_len;
}

/* Prepare T0 and T1 for a memory + alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static int dec_prep_alu_m(DisasContext *dc, int s_ext, int memsize,
			  TCGv dst, TCGv src)
{
	int insn_len;

	insn_len = dec_prep_move_m(dc, s_ext, memsize, src);
	tcg_gen_mov_tl(dst, cpu_R[dc->op2]);
	return insn_len;
}

#if DISAS_CRIS
static const char *cc_name(int cc)
{
	static const char *cc_names[16] = {
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

	LOG_DIS("b%s %x\n", cc_name(cond), dc->pc + offset);

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

	LOG_DIS("addoq %d, $r%u\n", imm, dc->op2);
	cris_cc_mask(dc, 0);
	/* Fetch register operand,  */
	tcg_gen_addi_tl(cpu_R[R_ACR], cpu_R[dc->op2], imm);

	return 2;
}
static unsigned int dec_addq(DisasContext *dc)
{
	LOG_DIS("addq %u, $r%u\n", dc->op1, dc->op2);

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	cris_cc_mask(dc, CC_MASK_NZVC);

	cris_alu(dc, CC_OP_ADD,
		    cpu_R[dc->op2], cpu_R[dc->op2], tcg_const_tl(dc->op1), 4);
	return 2;
}
static unsigned int dec_moveq(DisasContext *dc)
{
	uint32_t imm;

	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);
	LOG_DIS("moveq %d, $r%u\n", imm, dc->op2);

	tcg_gen_mov_tl(cpu_R[dc->op2], tcg_const_tl(imm));
	return 2;
}
static unsigned int dec_subq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

	LOG_DIS("subq %u, $r%u\n", dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_SUB,
		    cpu_R[dc->op2], cpu_R[dc->op2], tcg_const_tl(dc->op1), 4);
	return 2;
}
static unsigned int dec_cmpq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);

	LOG_DIS("cmpq %d, $r%d\n", imm, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZVC);

	cris_alu(dc, CC_OP_CMP,
		    cpu_R[dc->op2], cpu_R[dc->op2], tcg_const_tl(imm), 4);
	return 2;
}
static unsigned int dec_andq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);

	LOG_DIS("andq %d, $r%d\n", imm, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);

	cris_alu(dc, CC_OP_AND,
		    cpu_R[dc->op2], cpu_R[dc->op2], tcg_const_tl(imm), 4);
	return 2;
}
static unsigned int dec_orq(DisasContext *dc)
{
	uint32_t imm;
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
	imm = sign_extend(dc->op1, 5);
	LOG_DIS("orq %d, $r%d\n", imm, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);

	cris_alu(dc, CC_OP_OR,
		    cpu_R[dc->op2], cpu_R[dc->op2], tcg_const_tl(imm), 4);
	return 2;
}
static unsigned int dec_btstq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	LOG_DIS("btstq %u, $r%d\n", dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	cris_evaluate_flags(dc);
	gen_helper_btst(cpu_PR[PR_CCS], cpu_R[dc->op2],
			tcg_const_tl(dc->op1), cpu_PR[PR_CCS]);
	cris_alu(dc, CC_OP_MOVE,
		 cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op2], 4);
	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	dc->flags_uptodate = 1;
	return 2;
}
static unsigned int dec_asrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	LOG_DIS("asrq %u, $r%d\n", dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);

	tcg_gen_sari_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);
	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2],
		    cpu_R[dc->op2], cpu_R[dc->op2], 4);
	return 2;
}
static unsigned int dec_lslq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	LOG_DIS("lslq %u, $r%d\n", dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);

	tcg_gen_shli_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);

	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2],
		    cpu_R[dc->op2], cpu_R[dc->op2], 4);
	return 2;
}
static unsigned int dec_lsrq(DisasContext *dc)
{
	dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
	LOG_DIS("lsrq %u, $r%d\n", dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);

	tcg_gen_shri_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);
	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2],
		    cpu_R[dc->op2], cpu_R[dc->op2], 4);
	return 2;
}

static unsigned int dec_move_r(DisasContext *dc)
{
	int size = memsize_zz(dc);

	LOG_DIS("move.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	if (size == 4) {
		dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, cpu_R[dc->op2]);
		cris_cc_mask(dc, CC_MASK_NZ);
		cris_update_cc_op(dc, CC_OP_MOVE, 4);
		cris_update_cc_x(dc);
		cris_update_result(dc, cpu_R[dc->op2]);
	}
	else {
		TCGv t0;

		t0 = tcg_temp_new();
		dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, t0);
		cris_alu(dc, CC_OP_MOVE,
			 cpu_R[dc->op2],
			 cpu_R[dc->op2], t0, size);
		tcg_temp_free(t0);
	}
	return 2;
}

static unsigned int dec_scc_r(DisasContext *dc)
{
	int cond = dc->op2;

	LOG_DIS("s%s $r%u\n",
		    cc_name(cond), dc->op1);

	if (cond != CC_A)
	{
		int l1;

		gen_tst_cc (dc, cpu_R[dc->op1], cond);
		l1 = gen_new_label();
		tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[dc->op1], 0, l1);
		tcg_gen_movi_tl(cpu_R[dc->op1], 1);
		gen_set_label(l1);
	}
	else
		tcg_gen_movi_tl(cpu_R[dc->op1], 1);

	cris_cc_mask(dc, 0);
	return 2;
}

static inline void cris_alu_alloc_temps(DisasContext *dc, int size, TCGv *t)
{
	if (size == 4) {
		t[0] = cpu_R[dc->op2];
		t[1] = cpu_R[dc->op1];
	} else {
		t[0] = tcg_temp_new();
		t[1] = tcg_temp_new();
	}
}

static inline void cris_alu_free_temps(DisasContext *dc, int size, TCGv *t)
{
	if (size != 4) {
		tcg_temp_free(t[0]);
		tcg_temp_free(t[1]);
	}
}

static unsigned int dec_and_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("and.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);

	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
	cris_alu(dc, CC_OP_AND, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_lz_r(DisasContext *dc)
{
	TCGv t0;
	LOG_DIS("lz $r%u, $r%u\n",
		    dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);
	t0 = tcg_temp_new();
	dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0, cpu_R[dc->op2], t0);
	cris_alu(dc, CC_OP_LZ, cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_lsl_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("lsl.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
	tcg_gen_andi_tl(t[1], t[1], 63);
	cris_alu(dc, CC_OP_LSL, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_alloc_temps(dc, size, t);
	return 2;
}

static unsigned int dec_lsr_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("lsr.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
	tcg_gen_andi_tl(t[1], t[1], 63);
	cris_alu(dc, CC_OP_LSR, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_asr_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("asr.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1, t[0], t[1]);
	tcg_gen_andi_tl(t[1], t[1], 63);
	cris_alu(dc, CC_OP_ASR, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_muls_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("muls.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZV);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1, t[0], t[1]);

	cris_alu(dc, CC_OP_MULS, cpu_R[dc->op2], t[0], t[1], 4);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_mulu_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);

	LOG_DIS("mulu.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZV);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

	cris_alu(dc, CC_OP_MULU, cpu_R[dc->op2], t[0], t[1], 4);
	cris_alu_alloc_temps(dc, size, t);
	return 2;
}


static unsigned int dec_dstep_r(DisasContext *dc)
{
	LOG_DIS("dstep $r%u, $r%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_DSTEP,
		    cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op1], 4);
	return 2;
}

static unsigned int dec_xor_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("xor.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	BUG_ON(size != 4); /* xor is dword.  */
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

	cris_alu(dc, CC_OP_XOR, cpu_R[dc->op2], t[0], t[1], 4);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_bound_r(DisasContext *dc)
{
	TCGv l0;
	int size = memsize_zz(dc);
	LOG_DIS("bound.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);
	l0 = tcg_temp_local_new();
	dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, l0);
	cris_alu(dc, CC_OP_BOUND, cpu_R[dc->op2], cpu_R[dc->op2], l0, 4);
	tcg_temp_free(l0);
	return 2;
}

static unsigned int dec_cmp_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("cmp.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

	cris_alu(dc, CC_OP_CMP, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_abs_r(DisasContext *dc)
{
	TCGv t0;

	LOG_DIS("abs $r%u, $r%u\n",
		    dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);

	t0 = tcg_temp_new();
	tcg_gen_sari_tl(t0, cpu_R[dc->op1], 31);
	tcg_gen_xor_tl(cpu_R[dc->op2], cpu_R[dc->op1], t0);
	tcg_gen_sub_tl(cpu_R[dc->op2], cpu_R[dc->op2], t0);
	tcg_temp_free(t0);

	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op2], 4);
	return 2;
}

static unsigned int dec_add_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("add.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

	cris_alu(dc, CC_OP_ADD, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_addc_r(DisasContext *dc)
{
	LOG_DIS("addc $r%u, $r%u\n",
		    dc->op1, dc->op2);
	cris_evaluate_flags(dc);
	/* Set for this insn.  */
	dc->flagx_known = 1;
	dc->flags_x = X_FLAG;

	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_ADDC,
		 cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op1], 4);
	return 2;
}

static unsigned int dec_mcp_r(DisasContext *dc)
{
	LOG_DIS("mcp $p%u, $r%u\n",
		     dc->op2, dc->op1);
	cris_evaluate_flags(dc);
	cris_cc_mask(dc, CC_MASK_RNZV);
	cris_alu(dc, CC_OP_MCP,
		    cpu_R[dc->op1], cpu_R[dc->op1], cpu_PR[dc->op2], 4);
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
	TCGv t0;
#if DISAS_CRIS
	char modename[4];
#endif
	LOG_DIS("swap%s $r%u\n",
		     swapmode_name(dc->op2, modename), dc->op1);

	cris_cc_mask(dc, CC_MASK_NZ);
	t0 = tcg_temp_new();
	t_gen_mov_TN_reg(t0, dc->op1);
	if (dc->op2 & 8)
		tcg_gen_not_tl(t0, t0);
	if (dc->op2 & 4)
		t_gen_swapw(t0, t0);
	if (dc->op2 & 2)
		t_gen_swapb(t0, t0);
	if (dc->op2 & 1)
		t_gen_swapr(t0, t0);
	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op1], cpu_R[dc->op1], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_or_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("or.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
	cris_alu(dc, CC_OP_OR, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_addi_r(DisasContext *dc)
{
	TCGv t0;
	LOG_DIS("addi.%c $r%u, $r%u\n",
		    memsize_char(memsize_zz(dc)), dc->op2, dc->op1);
	cris_cc_mask(dc, 0);
	t0 = tcg_temp_new();
	tcg_gen_shl_tl(t0, cpu_R[dc->op2], tcg_const_tl(dc->zzsize));
	tcg_gen_add_tl(cpu_R[dc->op1], cpu_R[dc->op1], t0);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_addi_acr(DisasContext *dc)
{
	TCGv t0;
	LOG_DIS("addi.%c $r%u, $r%u, $acr\n",
		  memsize_char(memsize_zz(dc)), dc->op2, dc->op1);
	cris_cc_mask(dc, 0);
	t0 = tcg_temp_new();
	tcg_gen_shl_tl(t0, cpu_R[dc->op2], tcg_const_tl(dc->zzsize));
	tcg_gen_add_tl(cpu_R[R_ACR], cpu_R[dc->op1], t0);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_neg_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("neg.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

	cris_alu(dc, CC_OP_NEG, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

static unsigned int dec_btst_r(DisasContext *dc)
{
	LOG_DIS("btst $r%u, $r%u\n",
		    dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_evaluate_flags(dc);
	gen_helper_btst(cpu_PR[PR_CCS], cpu_R[dc->op2],
			cpu_R[dc->op1], cpu_PR[PR_CCS]);
	cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2],
		 cpu_R[dc->op2], cpu_R[dc->op2], 4);
	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	dc->flags_uptodate = 1;
	return 2;
}

static unsigned int dec_sub_r(DisasContext *dc)
{
	TCGv t[2];
	int size = memsize_zz(dc);
	LOG_DIS("sub.%c $r%u, $r%u\n",
		    memsize_char(size), dc->op1, dc->op2);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu_alloc_temps(dc, size, t);
	dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
	cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], t[0], t[1], size);
	cris_alu_free_temps(dc, size, t);
	return 2;
}

/* Zero extension. From size to dword.  */
static unsigned int dec_movu_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("movu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	t0 = tcg_temp_new();
	dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, t0);
	cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

/* Sign extension. From size to dword.  */
static unsigned int dec_movs_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("movs.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZ);
	t0 = tcg_temp_new();
	/* Size can only be qi or hi.  */
	t_gen_sext(t0, cpu_R[dc->op1], size);
	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2], cpu_R[dc->op1], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

/* zero extension. From size to dword.  */
static unsigned int dec_addu_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("addu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZVC);
	t0 = tcg_temp_new();
	/* Size can only be qi or hi.  */
	t_gen_zext(t0, cpu_R[dc->op1], size);
	cris_alu(dc, CC_OP_ADD,
		    cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

/* Sign extension. From size to dword.  */
static unsigned int dec_adds_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("adds.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZVC);
	t0 = tcg_temp_new();
	/* Size can only be qi or hi.  */
	t_gen_sext(t0, cpu_R[dc->op1], size);
	cris_alu(dc, CC_OP_ADD,
		    cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

/* Zero extension. From size to dword.  */
static unsigned int dec_subu_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("subu.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZVC);
	t0 = tcg_temp_new();
	/* Size can only be qi or hi.  */
	t_gen_zext(t0, cpu_R[dc->op1], size);
	cris_alu(dc, CC_OP_SUB,
		    cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

/* Sign extension. From size to dword.  */
static unsigned int dec_subs_r(DisasContext *dc)
{
	TCGv t0;
	int size = memsize_z(dc);
	LOG_DIS("subs.%c $r%u, $r%u\n",
		    memsize_char(size),
		    dc->op1, dc->op2);

	cris_cc_mask(dc, CC_MASK_NZVC);
	t0 = tcg_temp_new();
	/* Size can only be qi or hi.  */
	t_gen_sext(t0, cpu_R[dc->op1], size);
	cris_alu(dc, CC_OP_SUB,
		    cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_setclrf(DisasContext *dc)
{
	uint32_t flags;
	int set = (~dc->opcode >> 2) & 1;


	flags = (EXTRACT_FIELD(dc->ir, 12, 15) << 4)
		| EXTRACT_FIELD(dc->ir, 0, 3);
	if (set && flags == 0) {
		LOG_DIS("nop\n");
		return 2;
	} else if (!set && (flags & 0x20)) {
		LOG_DIS("di\n");
	}
	else {
		LOG_DIS("%sf %x\n",
			     set ? "set" : "clr",
			    flags);
	}

	/* User space is not allowed to touch these. Silently ignore.  */
	if (dc->tb_flags & U_FLAG) {
		flags &= ~(S_FLAG | I_FLAG | U_FLAG);
	}

	if (flags & X_FLAG) {
		dc->flagx_known = 1;
		if (set)
			dc->flags_x = X_FLAG;
		else
			dc->flags_x = 0;
	}

	/* Break the TB if the P flag changes.  */
	if (flags & P_FLAG) {
		if ((set && !(dc->tb_flags & P_FLAG))
		    || (!set && (dc->tb_flags & P_FLAG))) {
			tcg_gen_movi_tl(env_pc, dc->pc + 2);
			dc->is_jmp = DISAS_UPDATE;
			dc->cpustate_changed = 1;
		}
	}
	if (flags & S_FLAG) {
		dc->cpustate_changed = 1;
	}


	/* Simply decode the flags.  */
	cris_evaluate_flags (dc);
	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	cris_update_cc_x(dc);
	tcg_gen_movi_tl(cc_op, dc->cc_op);

	if (set) {
		if (!(dc->tb_flags & U_FLAG) && (flags & U_FLAG)) {
			/* Enter user mode.  */
			t_gen_mov_env_TN(ksp, cpu_R[R_SP]);
			tcg_gen_mov_tl(cpu_R[R_SP], cpu_PR[PR_USP]);
			dc->cpustate_changed = 1;
		}
		tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], flags);
	}
	else
		tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~flags);

	dc->flags_uptodate = 1;
	dc->clear_x = 0;
	return 2;
}

static unsigned int dec_move_rs(DisasContext *dc)
{
	LOG_DIS("move $r%u, $s%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, 0);
	gen_helper_movl_sreg_reg(tcg_const_tl(dc->op2), tcg_const_tl(dc->op1));
	return 2;
}
static unsigned int dec_move_sr(DisasContext *dc)
{
	LOG_DIS("move $s%u, $r%u\n", dc->op2, dc->op1);
	cris_cc_mask(dc, 0);
	gen_helper_movl_reg_sreg(tcg_const_tl(dc->op1), tcg_const_tl(dc->op2));
	return 2;
}

static unsigned int dec_move_rp(DisasContext *dc)
{
	TCGv t[2];
	LOG_DIS("move $r%u, $p%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, 0);

	t[0] = tcg_temp_new();
	if (dc->op2 == PR_CCS) {
		cris_evaluate_flags(dc);
		t_gen_mov_TN_reg(t[0], dc->op1);
		if (dc->tb_flags & U_FLAG) {
			t[1] = tcg_temp_new();
			/* User space is not allowed to touch all flags.  */
			tcg_gen_andi_tl(t[0], t[0], 0x39f);
			tcg_gen_andi_tl(t[1], cpu_PR[PR_CCS], ~0x39f);
			tcg_gen_or_tl(t[0], t[1], t[0]);
			tcg_temp_free(t[1]);
		}
	}
	else
		t_gen_mov_TN_reg(t[0], dc->op1);

	t_gen_mov_preg_TN(dc, dc->op2, t[0]);
	if (dc->op2 == PR_CCS) {
		cris_update_cc_op(dc, CC_OP_FLAGS, 4);
		dc->flags_uptodate = 1;
	}
	tcg_temp_free(t[0]);
	return 2;
}
static unsigned int dec_move_pr(DisasContext *dc)
{
	TCGv t0;
	LOG_DIS("move $p%u, $r%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, 0);

	if (dc->op2 == PR_CCS)
		cris_evaluate_flags(dc);

	t0 = tcg_temp_new();
	t_gen_mov_TN_preg(t0, dc->op2);
	cris_alu(dc, CC_OP_MOVE,
		 cpu_R[dc->op1], cpu_R[dc->op1], t0, preg_sizes[dc->op2]);
	tcg_temp_free(t0);
	return 2;
}

static unsigned int dec_move_mr(DisasContext *dc)
{
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("move.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	if (memsize == 4) {
		insn_len = dec_prep_move_m(dc, 0, 4, cpu_R[dc->op2]);
		cris_cc_mask(dc, CC_MASK_NZ);
		cris_update_cc_op(dc, CC_OP_MOVE, 4);
		cris_update_cc_x(dc);
		cris_update_result(dc, cpu_R[dc->op2]);
	}
	else {
		TCGv t0;

		t0 = tcg_temp_new();
		insn_len = dec_prep_move_m(dc, 0, memsize, t0);
		cris_cc_mask(dc, CC_MASK_NZ);
		cris_alu(dc, CC_OP_MOVE,
			    cpu_R[dc->op2], cpu_R[dc->op2], t0, memsize);
		tcg_temp_free(t0);
	}
	do_postinc(dc, memsize);
	return insn_len;
}

static inline void cris_alu_m_alloc_temps(TCGv *t)
{
	t[0] = tcg_temp_new();
	t[1] = tcg_temp_new();
}

static inline void cris_alu_m_free_temps(TCGv *t)
{
	tcg_temp_free(t[0]);
	tcg_temp_free(t[1]);
}

static unsigned int dec_movs_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("movs.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	/* sign extend.  */
	insn_len = dec_prep_alu_m(dc, 1, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_MOVE,
		    cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_addu_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("addu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	/* sign extend.  */
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_ADD,
		    cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_adds_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("adds.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	/* sign extend.  */
	insn_len = dec_prep_alu_m(dc, 1, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_ADD, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_subu_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("subu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	/* sign extend.  */
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_subs_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("subs.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	/* sign extend.  */
	insn_len = dec_prep_alu_m(dc, 1, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_movu_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;

	LOG_DIS("movu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_cmpu_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("cmpu.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_CMP, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_cmps_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_z(dc);
	int insn_len;
	LOG_DIS("cmps.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 1, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_CMP,
		    cpu_R[dc->op2], cpu_R[dc->op2], t[1],
		    memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_cmp_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("cmp.%c [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_CMP,
		    cpu_R[dc->op2], cpu_R[dc->op2], t[1],
		    memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_test_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("test.%d [$r%u%s] op2=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_evaluate_flags(dc);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~3);

	cris_alu(dc, CC_OP_CMP,
		 cpu_R[dc->op2], t[1], tcg_const_tl(0), memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_and_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("and.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_AND, cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_add_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("add.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_ADD,
		 cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_addo_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("add.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 1, memsize, t[0], t[1]);
	cris_cc_mask(dc, 0);
	cris_alu(dc, CC_OP_ADD, cpu_R[R_ACR], t[0], t[1], 4);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_bound_m(DisasContext *dc)
{
	TCGv l[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("bound.%d [$r%u%s, $r%u\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	l[0] = tcg_temp_local_new();
	l[1] = tcg_temp_local_new();
	insn_len = dec_prep_alu_m(dc, 0, memsize, l[0], l[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_BOUND, cpu_R[dc->op2], l[0], l[1], 4);
	do_postinc(dc, memsize);
	tcg_temp_free(l[0]);
	tcg_temp_free(l[1]);
	return insn_len;
}

static unsigned int dec_addc_mr(DisasContext *dc)
{
	TCGv t[2];
	int insn_len = 2;
	LOG_DIS("addc [$r%u%s, $r%u\n",
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_evaluate_flags(dc);

	/* Set for this insn.  */
	dc->flagx_known = 1;
	dc->flags_x = X_FLAG;

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, 4, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_ADDC, cpu_R[dc->op2], t[0], t[1], 4);
	do_postinc(dc, 4);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_sub_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("sub.%c [$r%u%s, $r%u ir=%x zz=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2, dc->ir, dc->zzsize);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZVC);
	cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], t[0], t[1], memsize);
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_or_m(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len;
	LOG_DIS("or.%d [$r%u%s, $r%u pc=%x\n",
		    memsize_char(memsize),
		    dc->op1, dc->postinc ? "+]" : "]",
		    dc->op2, dc->pc);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, CC_MASK_NZ);
	cris_alu(dc, CC_OP_OR,
		    cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_move_mp(DisasContext *dc)
{
	TCGv t[2];
	int memsize = memsize_zz(dc);
	int insn_len = 2;

	LOG_DIS("move.%c [$r%u%s, $p%u\n",
		    memsize_char(memsize),
		    dc->op1,
		    dc->postinc ? "+]" : "]",
		    dc->op2);

	cris_alu_m_alloc_temps(t);
	insn_len = dec_prep_alu_m(dc, 0, memsize, t[0], t[1]);
	cris_cc_mask(dc, 0);
	if (dc->op2 == PR_CCS) {
		cris_evaluate_flags(dc);
		if (dc->tb_flags & U_FLAG) {
			/* User space is not allowed to touch all flags.  */
			tcg_gen_andi_tl(t[1], t[1], 0x39f);
			tcg_gen_andi_tl(t[0], cpu_PR[PR_CCS], ~0x39f);
			tcg_gen_or_tl(t[1], t[0], t[1]);
		}
	}

	t_gen_mov_preg_TN(dc, dc->op2, t[1]);

	do_postinc(dc, memsize);
	cris_alu_m_free_temps(t);
	return insn_len;
}

static unsigned int dec_move_pm(DisasContext *dc)
{
	TCGv t0;
	int memsize;

	memsize = preg_sizes[dc->op2];

	LOG_DIS("move.%c $p%u, [$r%u%s\n",
		     memsize_char(memsize), 
		     dc->op2, dc->op1, dc->postinc ? "+]" : "]");

	/* prepare store. Address in T0, value in T1.  */
	if (dc->op2 == PR_CCS)
		cris_evaluate_flags(dc);
	t0 = tcg_temp_new();
	t_gen_mov_TN_preg(t0, dc->op2);
	cris_flush_cc_state(dc);
	gen_store(dc, cpu_R[dc->op1], t0, memsize);
	tcg_temp_free(t0);

	cris_cc_mask(dc, 0);
	if (dc->postinc)
		tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], memsize);
	return 2;
}

static unsigned int dec_movem_mr(DisasContext *dc)
{
	TCGv_i64 tmp[16];
        TCGv tmp32;
	TCGv addr;
	int i;
	int nr = dc->op2 + 1;

	LOG_DIS("movem [$r%u%s, $r%u\n", dc->op1,
		    dc->postinc ? "+]" : "]", dc->op2);

	addr = tcg_temp_new();
	/* There are probably better ways of doing this.  */
	cris_flush_cc_state(dc);
	for (i = 0; i < (nr >> 1); i++) {
		tmp[i] = tcg_temp_new_i64();
		tcg_gen_addi_tl(addr, cpu_R[dc->op1], i * 8);
		gen_load64(dc, tmp[i], addr);
	}
	if (nr & 1) {
		tmp32 = tcg_temp_new_i32();
		tcg_gen_addi_tl(addr, cpu_R[dc->op1], i * 8);
		gen_load(dc, tmp32, addr, 4, 0);
	}
	tcg_temp_free(addr);

	for (i = 0; i < (nr >> 1); i++) {
		tcg_gen_trunc_i64_i32(cpu_R[i * 2], tmp[i]);
		tcg_gen_shri_i64(tmp[i], tmp[i], 32);
		tcg_gen_trunc_i64_i32(cpu_R[i * 2 + 1], tmp[i]);
		tcg_temp_free_i64(tmp[i]);
	}
	if (nr & 1) {
		tcg_gen_mov_tl(cpu_R[dc->op2], tmp32);
		tcg_temp_free(tmp32);
	}

	/* writeback the updated pointer value.  */
	if (dc->postinc)
		tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], nr * 4);

	/* gen_load might want to evaluate the previous insns flags.  */
	cris_cc_mask(dc, 0);
	return 2;
}

static unsigned int dec_movem_rm(DisasContext *dc)
{
	TCGv tmp;
	TCGv addr;
	int i;

	LOG_DIS("movem $r%u, [$r%u%s\n", dc->op2, dc->op1,
		     dc->postinc ? "+]" : "]");

	cris_flush_cc_state(dc);

	tmp = tcg_temp_new();
	addr = tcg_temp_new();
	tcg_gen_movi_tl(tmp, 4);
	tcg_gen_mov_tl(addr, cpu_R[dc->op1]);
	for (i = 0; i <= dc->op2; i++) {
		/* Displace addr.  */
		/* Perform the store.  */
		gen_store(dc, addr, cpu_R[i], 4);
		tcg_gen_add_tl(addr, addr, tmp);
	}
	if (dc->postinc)
		tcg_gen_mov_tl(cpu_R[dc->op1], addr);
	cris_cc_mask(dc, 0);
	tcg_temp_free(tmp);
	tcg_temp_free(addr);
	return 2;
}

static unsigned int dec_move_rm(DisasContext *dc)
{
	int memsize;

	memsize = memsize_zz(dc);

	LOG_DIS("move.%d $r%u, [$r%u]\n",
		     memsize, dc->op2, dc->op1);

	/* prepare store.  */
	cris_flush_cc_state(dc);
	gen_store(dc, cpu_R[dc->op1], cpu_R[dc->op2], memsize);

	if (dc->postinc)
		tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], memsize);
	cris_cc_mask(dc, 0);
	return 2;
}

static unsigned int dec_lapcq(DisasContext *dc)
{
	LOG_DIS("lapcq %x, $r%u\n",
		    dc->pc + dc->op1*2, dc->op2);
	cris_cc_mask(dc, 0);
	tcg_gen_movi_tl(cpu_R[dc->op2], dc->pc + dc->op1 * 2);
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
	LOG_DIS("lapc 0x%x, $r%u\n", imm + dc->pc, dc->op2);

	pc = dc->pc;
	pc += imm;
	t_gen_mov_reg_TN(rd, tcg_const_tl(pc));
	return 6;
}

/* Jump to special reg.  */
static unsigned int dec_jump_p(DisasContext *dc)
{
	LOG_DIS("jump $p%u\n", dc->op2);

	if (dc->op2 == PR_CCS)
		cris_evaluate_flags(dc);
	t_gen_mov_TN_preg(env_btarget, dc->op2);
	/* rete will often have low bit set to indicate delayslot.  */
	tcg_gen_andi_tl(env_btarget, env_btarget, ~1);
	cris_cc_mask(dc, 0);
	cris_prepare_jmp(dc, JMP_INDIRECT);
	return 2;
}

/* Jump and save.  */
static unsigned int dec_jas_r(DisasContext *dc)
{
	LOG_DIS("jas $r%u, $p%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	tcg_gen_mov_tl(env_btarget, cpu_R[dc->op1]);
	if (dc->op2 > 15)
		abort();
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 4));

	cris_prepare_jmp(dc, JMP_INDIRECT);
	return 2;
}

static unsigned int dec_jas_im(DisasContext *dc)
{
	uint32_t imm;

	imm = ldl_code(dc->pc + 2);

	LOG_DIS("jas 0x%x\n", imm);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 8));

	dc->jmp_pc = imm;
	cris_prepare_jmp(dc, JMP_DIRECT);
	return 6;
}

static unsigned int dec_jasc_im(DisasContext *dc)
{
	uint32_t imm;

	imm = ldl_code(dc->pc + 2);

	LOG_DIS("jasc 0x%x\n", imm);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 8 + 4));

	dc->jmp_pc = imm;
	cris_prepare_jmp(dc, JMP_DIRECT);
	return 6;
}

static unsigned int dec_jasc_r(DisasContext *dc)
{
	LOG_DIS("jasc_r $r%u, $p%u\n", dc->op1, dc->op2);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	tcg_gen_mov_tl(env_btarget, cpu_R[dc->op1]);
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 4 + 4));
	cris_prepare_jmp(dc, JMP_INDIRECT);
	return 2;
}

static unsigned int dec_bcc_im(DisasContext *dc)
{
	int32_t offset;
	uint32_t cond = dc->op2;

	offset = ldsw_code(dc->pc + 2);

	LOG_DIS("b%s %d pc=%x dst=%x\n",
		    cc_name(cond), offset,
		    dc->pc, dc->pc + offset);

	cris_cc_mask(dc, 0);
	/* op2 holds the condition-code.  */
	cris_prepare_cc_branch (dc, offset, cond);
	return 4;
}

static unsigned int dec_bas_im(DisasContext *dc)
{
	int32_t simm;


	simm = ldl_code(dc->pc + 2);

	LOG_DIS("bas 0x%x, $p%u\n", dc->pc + simm, dc->op2);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 8));

	dc->jmp_pc = dc->pc + simm;
	cris_prepare_jmp(dc, JMP_DIRECT);
	return 6;
}

static unsigned int dec_basc_im(DisasContext *dc)
{
	int32_t simm;
	simm = ldl_code(dc->pc + 2);

	LOG_DIS("basc 0x%x, $p%u\n", dc->pc + simm, dc->op2);
	cris_cc_mask(dc, 0);
	/* Store the return address in Pd.  */
	t_gen_mov_preg_TN(dc, dc->op2, tcg_const_tl(dc->pc + 12));

	dc->jmp_pc = dc->pc + simm;
	cris_prepare_jmp(dc, JMP_DIRECT);
	return 6;
}

static unsigned int dec_rfe_etc(DisasContext *dc)
{
	cris_cc_mask(dc, 0);

	if (dc->op2 == 15) {
		t_gen_mov_env_TN(halted, tcg_const_tl(1));
		tcg_gen_movi_tl(env_pc, dc->pc + 2);
		t_gen_raise_exception(EXCP_HLT);
		return 2;
	}

	switch (dc->op2 & 7) {
		case 2:
			/* rfe.  */
			LOG_DIS("rfe\n");
			cris_evaluate_flags(dc);
			gen_helper_rfe();
			dc->is_jmp = DISAS_UPDATE;
			break;
		case 5:
			/* rfn.  */
			LOG_DIS("rfn\n");
			cris_evaluate_flags(dc);
			gen_helper_rfn();
			dc->is_jmp = DISAS_UPDATE;
			break;
		case 6:
			LOG_DIS("break %d\n", dc->op1);
			cris_evaluate_flags (dc);
			/* break.  */
			tcg_gen_movi_tl(env_pc, dc->pc + 2);

			/* Breaks start at 16 in the exception vector.  */
			t_gen_mov_env_TN(trap_vector, 
					 tcg_const_tl(dc->op1 + 16));
			t_gen_raise_exception(EXCP_BREAK);
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
	return 2;
}

static unsigned int dec_ftag_fidx_i_m(DisasContext *dc)
{
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

static struct decoder_info {
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
	int i;

	if (unlikely(loglevel & CPU_LOG_TB_OP))
		tcg_gen_debug_insn_start(dc->pc);

	/* Load a halfword onto the instruction register.  */
	dc->ir = lduw_code(dc->pc);

	/* Now decode it.  */
	dc->opcode   = EXTRACT_FIELD(dc->ir, 4, 11);
	dc->op1      = EXTRACT_FIELD(dc->ir, 0, 3);
	dc->op2      = EXTRACT_FIELD(dc->ir, 12, 15);
	dc->zsize    = EXTRACT_FIELD(dc->ir, 4, 4);
	dc->zzsize   = EXTRACT_FIELD(dc->ir, 4, 5);
	dc->postinc  = EXTRACT_FIELD(dc->ir, 10, 10);

	/* Large switch for all insns.  */
	for (i = 0; i < ARRAY_SIZE(decinfo); i++) {
		if ((dc->opcode & decinfo[i].mask) == decinfo[i].bits)
		{
			insn_len = decinfo[i].dec(dc);
			break;
		}
	}

#if !defined(CONFIG_USER_ONLY)
	/* Single-stepping ?  */
	if (dc->tb_flags & S_FLAG) {
		int l1;

		l1 = gen_new_label();
		tcg_gen_brcondi_tl(TCG_COND_NE, cpu_PR[PR_SPC], dc->pc, l1);
		/* We treat SPC as a break with an odd trap vector.  */
		cris_evaluate_flags (dc);
		t_gen_mov_env_TN(trap_vector, tcg_const_tl(3));
		tcg_gen_movi_tl(env_pc, dc->pc + insn_len);
		tcg_gen_movi_tl(cpu_PR[PR_SPC], dc->pc + insn_len);
		t_gen_raise_exception(EXCP_BREAK);
		gen_set_label(l1);
	}
#endif
	return insn_len;
}

static void check_breakpoint(CPUState *env, DisasContext *dc)
{
	CPUBreakpoint *bp;

	if (unlikely(!TAILQ_EMPTY(&env->breakpoints))) {
		TAILQ_FOREACH(bp, &env->breakpoints, entry) {
			if (bp->pc == dc->pc) {
				cris_evaluate_flags (dc);
				tcg_gen_movi_tl(env_pc, dc->pc);
				t_gen_raise_exception(EXCP_DEBUG);
				dc->is_jmp = DISAS_UPDATE;
			}
		}
	}
}


/*
 * Delay slots on QEMU/CRIS.
 *
 * If an exception hits on a delayslot, the core will let ERP (the Exception
 * Return Pointer) point to the branch (the previous) insn and set the lsb to
 * to give SW a hint that the exception actually hit on the dslot.
 *
 * CRIS expects all PC addresses to be 16-bit aligned. The lsb is ignored by
 * the core and any jmp to an odd addresses will mask off that lsb. It is 
 * simply there to let sw know there was an exception on a dslot.
 *
 * When the software returns from an exception, the branch will re-execute.
 * On QEMU care needs to be taken when a branch+delayslot sequence is broken
 * and the branch and delayslot dont share pages.
 *
 * The TB contaning the branch insn will set up env->btarget and evaluate 
 * env->btaken. When the translation loop exits we will note that the branch 
 * sequence is broken and let env->dslot be the size of the branch insn (those
 * vary in length).
 *
 * The TB contaning the delayslot will have the PC of its real insn (i.e no lsb
 * set). It will also expect to have env->dslot setup with the size of the 
 * delay slot so that env->pc - env->dslot point to the branch insn. This TB 
 * will execute the dslot and take the branch, either to btarget or just one 
 * insn ahead.
 *
 * When exceptions occur, we check for env->dslot in do_interrupt to detect 
 * broken branch sequences and setup $erp accordingly (i.e let it point to the
 * branch and set lsb). Then env->dslot gets cleared so that the exception 
 * handler can enter. When returning from exceptions (jump $erp) the lsb gets
 * masked off and we will reexecute the branch insn.
 *
 */

/* generate intermediate code for basic block 'tb'.  */
static void
gen_intermediate_code_internal(CPUState *env, TranslationBlock *tb,
                               int search_pc)
{
	uint16_t *gen_opc_end;
   	uint32_t pc_start;
	unsigned int insn_len;
	int j, lj;
	struct DisasContext ctx;
	struct DisasContext *dc = &ctx;
	uint32_t next_page_start;
	target_ulong npc;
        int num_insns;
        int max_insns;

	qemu_log_try_set_file(stderr);

	/* Odd PC indicates that branch is rexecuting due to exception in the
	 * delayslot, like in real hw.
	 */
	pc_start = tb->pc & ~1;
	dc->env = env;
	dc->tb = tb;

	gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

	dc->is_jmp = DISAS_NEXT;
	dc->ppc = pc_start;
	dc->pc = pc_start;
	dc->singlestep_enabled = env->singlestep_enabled;
	dc->flags_uptodate = 1;
	dc->flagx_known = 1;
	dc->flags_x = tb->flags & X_FLAG;
	dc->cc_x_uptodate = 0;
	dc->cc_mask = 0;
	dc->update_cc = 0;

	cris_update_cc_op(dc, CC_OP_FLAGS, 4);
	dc->cc_size_uptodate = -1;

	/* Decode TB flags.  */
	dc->tb_flags = tb->flags & (S_FLAG | P_FLAG | U_FLAG | X_FLAG);
	dc->delayed_branch = !!(tb->flags & 7);
	if (dc->delayed_branch)
		dc->jmp = JMP_INDIRECT;
	else
		dc->jmp = JMP_NOJMP;

	dc->cpustate_changed = 0;

	if (loglevel & CPU_LOG_TB_IN_ASM) {
		qemu_log(
			"srch=%d pc=%x %x flg=%llx bt=%x ds=%u ccs=%x\n"
			"pid=%x usp=%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n"
			"%x.%x.%x.%x\n",
			search_pc, dc->pc, dc->ppc,
			(unsigned long long)tb->flags,
			env->btarget, (unsigned)tb->flags & 7,
			env->pregs[PR_CCS], 
			env->pregs[PR_PID], env->pregs[PR_USP],
			env->regs[0], env->regs[1], env->regs[2], env->regs[3],
			env->regs[4], env->regs[5], env->regs[6], env->regs[7],
			env->regs[8], env->regs[9],
			env->regs[10], env->regs[11],
			env->regs[12], env->regs[13],
			env->regs[14], env->regs[15]);
		qemu_log("--------------\n");
		qemu_log("IN: %s\n", lookup_symbol(pc_start));
	}

	next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
	lj = -1;
        num_insns = 0;
        max_insns = tb->cflags & CF_COUNT_MASK;
        if (max_insns == 0)
            max_insns = CF_COUNT_MASK;

        gen_icount_start();
	do
	{
		check_breakpoint(env, dc);

		if (search_pc) {
			j = gen_opc_ptr - gen_opc_buf;
			if (lj < j) {
				lj++;
				while (lj < j)
					gen_opc_instr_start[lj++] = 0;
			}
			if (dc->delayed_branch == 1)
				gen_opc_pc[lj] = dc->ppc | 1;
			else
				gen_opc_pc[lj] = dc->pc;
			gen_opc_instr_start[lj] = 1;
                        gen_opc_icount[lj] = num_insns;
		}

		/* Pretty disas.  */
		LOG_DIS("%8.8x:\t", dc->pc);

                if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
                    gen_io_start();
		dc->clear_x = 1;

		insn_len = cris_decoder(dc);		
		dc->ppc = dc->pc;
		dc->pc += insn_len;
		if (dc->clear_x)
			cris_clear_x_flag(dc);

                num_insns++;
		/* Check for delayed branches here. If we do it before
		   actually generating any host code, the simulator will just
		   loop doing nothing for on this program location.  */
		if (dc->delayed_branch) {
			dc->delayed_branch--;
			if (dc->delayed_branch == 0)
			{
				if (tb->flags & 7)
					t_gen_mov_env_TN(dslot, 
						tcg_const_tl(0));
				if (dc->jmp == JMP_DIRECT) {
					dc->is_jmp = DISAS_NEXT;
				} else {
					t_gen_cc_jmp(env_btarget, 
						     tcg_const_tl(dc->pc));
					dc->is_jmp = DISAS_JUMP;
				}
				break;
			}
		}

		/* If we are rexecuting a branch due to exceptions on
		   delay slots dont break.  */
		if (!(tb->pc & 1) && env->singlestep_enabled)
			break;
	} while (!dc->is_jmp && !dc->cpustate_changed
		 && gen_opc_ptr < gen_opc_end
		 && (dc->pc < next_page_start)
                 && num_insns < max_insns);

	npc = dc->pc;
	if (dc->jmp == JMP_DIRECT && !dc->delayed_branch)
		npc = dc->jmp_pc;

        if (tb->cflags & CF_LAST_IO)
            gen_io_end();
	/* Force an update if the per-tb cpu state has changed.  */
	if (dc->is_jmp == DISAS_NEXT
	    && (dc->cpustate_changed || !dc->flagx_known 
	    || (dc->flags_x != (tb->flags & X_FLAG)))) {
		dc->is_jmp = DISAS_UPDATE;
		tcg_gen_movi_tl(env_pc, npc);
	}
	/* Broken branch+delayslot sequence.  */
	if (dc->delayed_branch == 1) {
		/* Set env->dslot to the size of the branch insn.  */
		t_gen_mov_env_TN(dslot, tcg_const_tl(dc->pc - dc->ppc));
		cris_store_direct_jmp(dc);
	}

	cris_evaluate_flags (dc);

	if (unlikely(env->singlestep_enabled)) {
		if (dc->is_jmp == DISAS_NEXT)
			tcg_gen_movi_tl(env_pc, npc);
		t_gen_raise_exception(EXCP_DEBUG);
	} else {
		switch(dc->is_jmp) {
			case DISAS_NEXT:
				gen_goto_tb(dc, 1, npc);
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
        gen_icount_end(tb, num_insns);
	*gen_opc_ptr = INDEX_op_end;
	if (search_pc) {
		j = gen_opc_ptr - gen_opc_buf;
		lj++;
		while (lj <= j)
			gen_opc_instr_start[lj++] = 0;
	} else {
		tb->size = dc->pc - pc_start;
                tb->icount = num_insns;
	}

#ifdef DEBUG_DISAS
#if !DISAS_CRIS
	if (loglevel & CPU_LOG_TB_IN_ASM) {
		log_target_disas(pc_start, dc->pc - pc_start, 0);
		qemu_log("\nisize=%d osize=%zd\n",
			dc->pc - pc_start, gen_opc_ptr - gen_opc_buf);
	}
#endif
#endif
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
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
		    "cc_op=%d cc_src=%d cc_dest=%d cc_result=%x cc_mask=%x\n",
		    env->pc, env->pregs[PR_CCS], env->btaken, env->btarget,
		    env->cc_op,
		    env->cc_src, env->cc_dest, env->cc_result, env->cc_mask);


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

CPUCRISState *cpu_cris_init (const char *cpu_model)
{
	CPUCRISState *env;
	static int tcg_initialized = 0;
	int i;

	env = qemu_mallocz(sizeof(CPUCRISState));
	if (!env)
		return NULL;

	cpu_exec_init(env);
	cpu_reset(env);

	if (tcg_initialized)
		return env;

	tcg_initialized = 1;

	cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
	cc_x = tcg_global_mem_new(TCG_AREG0,
				  offsetof(CPUState, cc_x), "cc_x");
	cc_src = tcg_global_mem_new(TCG_AREG0,
				    offsetof(CPUState, cc_src), "cc_src");
	cc_dest = tcg_global_mem_new(TCG_AREG0,
				     offsetof(CPUState, cc_dest),
				     "cc_dest");
	cc_result = tcg_global_mem_new(TCG_AREG0,
				       offsetof(CPUState, cc_result),
				       "cc_result");
	cc_op = tcg_global_mem_new(TCG_AREG0,
				   offsetof(CPUState, cc_op), "cc_op");
	cc_size = tcg_global_mem_new(TCG_AREG0,
				     offsetof(CPUState, cc_size),
				     "cc_size");
	cc_mask = tcg_global_mem_new(TCG_AREG0,
				     offsetof(CPUState, cc_mask),
				     "cc_mask");

	env_pc = tcg_global_mem_new(TCG_AREG0, 
				    offsetof(CPUState, pc),
				    "pc");
	env_btarget = tcg_global_mem_new(TCG_AREG0,
					 offsetof(CPUState, btarget),
					 "btarget");
	env_btaken = tcg_global_mem_new(TCG_AREG0,
					 offsetof(CPUState, btaken),
					 "btaken");
	for (i = 0; i < 16; i++) {
		cpu_R[i] = tcg_global_mem_new(TCG_AREG0,
					      offsetof(CPUState, regs[i]),
					      regnames[i]);
	}
	for (i = 0; i < 16; i++) {
		cpu_PR[i] = tcg_global_mem_new(TCG_AREG0,
					       offsetof(CPUState, pregs[i]),
					       pregnames[i]);
	}

#define GEN_HELPER 2
#include "helper.h"

	return env;
}

void cpu_reset (CPUCRISState *env)
{
	memset(env, 0, offsetof(CPUCRISState, breakpoints));
	tlb_flush(env, 1);

	env->pregs[PR_VR] = 32;
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
