/*
 *  SH4 translation
 *
 *  Copyright (c) 2005 Samuel Tardieu
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define DEBUG_DISAS

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "trace-tcg.h"
#include "exec/log.h"
#include "qemu/qemu-print.h"


typedef struct DisasContext {
    DisasContextBase base;

    uint32_t tbflags;  /* should stay unmodified during the TB translation */
    uint32_t envflags; /* should stay in sync with env->flags using TCG ops */
    int memidx;
    int gbank;
    int fbank;
    uint32_t delayed_pc;
    uint32_t features;

    uint16_t opcode;

    bool has_movcal;
} DisasContext;

#if defined(CONFIG_USER_ONLY)
#define IS_USER(ctx) 1
#else
#define IS_USER(ctx) (!(ctx->tbflags & (1u << SR_MD)))
#endif

/* Target-specific values for ctx->base.is_jmp.  */
/* We want to exit back to the cpu loop for some reason.
   Usually this is to recognize interrupts immediately.  */
#define DISAS_STOP    DISAS_TARGET_0

/* global register indexes */
static TCGv cpu_gregs[32];
static TCGv cpu_sr, cpu_sr_m, cpu_sr_q, cpu_sr_t;
static TCGv cpu_pc, cpu_ssr, cpu_spc, cpu_gbr;
static TCGv cpu_vbr, cpu_sgr, cpu_dbr, cpu_mach, cpu_macl;
static TCGv cpu_pr, cpu_fpscr, cpu_fpul;
static TCGv cpu_lock_addr, cpu_lock_value;
static TCGv cpu_fregs[32];

/* internal register indexes */
static TCGv cpu_flags, cpu_delayed_pc, cpu_delayed_cond;

#include "exec/gen-icount.h"

void sh4_translate_init(void)
{
    int i;
    static const char * const gregnames[24] = {
        "R0_BANK0", "R1_BANK0", "R2_BANK0", "R3_BANK0",
        "R4_BANK0", "R5_BANK0", "R6_BANK0", "R7_BANK0",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
        "R0_BANK1", "R1_BANK1", "R2_BANK1", "R3_BANK1",
        "R4_BANK1", "R5_BANK1", "R6_BANK1", "R7_BANK1"
    };
    static const char * const fregnames[32] = {
         "FPR0_BANK0",  "FPR1_BANK0",  "FPR2_BANK0",  "FPR3_BANK0",
         "FPR4_BANK0",  "FPR5_BANK0",  "FPR6_BANK0",  "FPR7_BANK0",
         "FPR8_BANK0",  "FPR9_BANK0", "FPR10_BANK0", "FPR11_BANK0",
        "FPR12_BANK0", "FPR13_BANK0", "FPR14_BANK0", "FPR15_BANK0",
         "FPR0_BANK1",  "FPR1_BANK1",  "FPR2_BANK1",  "FPR3_BANK1",
         "FPR4_BANK1",  "FPR5_BANK1",  "FPR6_BANK1",  "FPR7_BANK1",
         "FPR8_BANK1",  "FPR9_BANK1", "FPR10_BANK1", "FPR11_BANK1",
        "FPR12_BANK1", "FPR13_BANK1", "FPR14_BANK1", "FPR15_BANK1",
    };

    for (i = 0; i < 24; i++) {
        cpu_gregs[i] = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPUSH4State, gregs[i]),
                                              gregnames[i]);
    }
    memcpy(cpu_gregs + 24, cpu_gregs + 8, 8 * sizeof(TCGv));

    cpu_pc = tcg_global_mem_new_i32(cpu_env,
                                    offsetof(CPUSH4State, pc), "PC");
    cpu_sr = tcg_global_mem_new_i32(cpu_env,
                                    offsetof(CPUSH4State, sr), "SR");
    cpu_sr_m = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, sr_m), "SR_M");
    cpu_sr_q = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, sr_q), "SR_Q");
    cpu_sr_t = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, sr_t), "SR_T");
    cpu_ssr = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, ssr), "SSR");
    cpu_spc = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, spc), "SPC");
    cpu_gbr = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, gbr), "GBR");
    cpu_vbr = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, vbr), "VBR");
    cpu_sgr = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, sgr), "SGR");
    cpu_dbr = tcg_global_mem_new_i32(cpu_env,
                                     offsetof(CPUSH4State, dbr), "DBR");
    cpu_mach = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, mach), "MACH");
    cpu_macl = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, macl), "MACL");
    cpu_pr = tcg_global_mem_new_i32(cpu_env,
                                    offsetof(CPUSH4State, pr), "PR");
    cpu_fpscr = tcg_global_mem_new_i32(cpu_env,
                                       offsetof(CPUSH4State, fpscr), "FPSCR");
    cpu_fpul = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUSH4State, fpul), "FPUL");

    cpu_flags = tcg_global_mem_new_i32(cpu_env,
				       offsetof(CPUSH4State, flags), "_flags_");
    cpu_delayed_pc = tcg_global_mem_new_i32(cpu_env,
					    offsetof(CPUSH4State, delayed_pc),
					    "_delayed_pc_");
    cpu_delayed_cond = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPUSH4State,
                                                       delayed_cond),
                                              "_delayed_cond_");
    cpu_lock_addr = tcg_global_mem_new_i32(cpu_env,
                                           offsetof(CPUSH4State, lock_addr),
                                           "_lock_addr_");
    cpu_lock_value = tcg_global_mem_new_i32(cpu_env,
                                            offsetof(CPUSH4State, lock_value),
                                            "_lock_value_");

    for (i = 0; i < 32; i++)
        cpu_fregs[i] = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPUSH4State, fregs[i]),
                                              fregnames[i]);
}

void superh_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;
    int i;

    qemu_fprintf(f, "pc=0x%08x sr=0x%08x pr=0x%08x fpscr=0x%08x\n",
                 env->pc, cpu_read_sr(env), env->pr, env->fpscr);
    qemu_fprintf(f, "spc=0x%08x ssr=0x%08x gbr=0x%08x vbr=0x%08x\n",
                 env->spc, env->ssr, env->gbr, env->vbr);
    qemu_fprintf(f, "sgr=0x%08x dbr=0x%08x delayed_pc=0x%08x fpul=0x%08x\n",
                 env->sgr, env->dbr, env->delayed_pc, env->fpul);
    for (i = 0; i < 24; i += 4) {
        qemu_printf("r%d=0x%08x r%d=0x%08x r%d=0x%08x r%d=0x%08x\n",
		    i, env->gregs[i], i + 1, env->gregs[i + 1],
		    i + 2, env->gregs[i + 2], i + 3, env->gregs[i + 3]);
    }
    if (env->flags & DELAY_SLOT) {
        qemu_printf("in delay slot (delayed_pc=0x%08x)\n",
		    env->delayed_pc);
    } else if (env->flags & DELAY_SLOT_CONDITIONAL) {
        qemu_printf("in conditional delay slot (delayed_pc=0x%08x)\n",
		    env->delayed_pc);
    } else if (env->flags & DELAY_SLOT_RTE) {
        qemu_fprintf(f, "in rte delay slot (delayed_pc=0x%08x)\n",
                     env->delayed_pc);
    }
}

static void gen_read_sr(TCGv dst)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_shli_i32(t0, cpu_sr_q, SR_Q);
    tcg_gen_or_i32(dst, dst, t0);
    tcg_gen_shli_i32(t0, cpu_sr_m, SR_M);
    tcg_gen_or_i32(dst, dst, t0);
    tcg_gen_shli_i32(t0, cpu_sr_t, SR_T);
    tcg_gen_or_i32(dst, cpu_sr, t0);
    tcg_temp_free_i32(t0);
}

static void gen_write_sr(TCGv src)
{
    tcg_gen_andi_i32(cpu_sr, src,
                     ~((1u << SR_Q) | (1u << SR_M) | (1u << SR_T)));
    tcg_gen_extract_i32(cpu_sr_q, src, SR_Q, 1);
    tcg_gen_extract_i32(cpu_sr_m, src, SR_M, 1);
    tcg_gen_extract_i32(cpu_sr_t, src, SR_T, 1);
}

static inline void gen_save_cpu_state(DisasContext *ctx, bool save_pc)
{
    if (save_pc) {
        tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    }
    if (ctx->delayed_pc != (uint32_t) -1) {
        tcg_gen_movi_i32(cpu_delayed_pc, ctx->delayed_pc);
    }
    if ((ctx->tbflags & TB_FLAG_ENVFLAGS_MASK) != ctx->envflags) {
        tcg_gen_movi_i32(cpu_flags, ctx->envflags);
    }
}

static inline bool use_exit_tb(DisasContext *ctx)
{
    return (ctx->tbflags & GUSA_EXCLUSIVE) != 0;
}

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    /* Use a direct jump if in same page and singlestep not enabled */
    if (unlikely(ctx->base.singlestep_enabled || use_exit_tb(ctx))) {
        return false;
    }
#ifndef CONFIG_USER_ONLY
    return (ctx->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else if (use_exit_tb(ctx)) {
            tcg_gen_exit_tb(NULL, 0);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_jump(DisasContext * ctx)
{
    if (ctx->delayed_pc == -1) {
	/* Target is not statically known, it comes necessarily from a
	   delayed jump as immediate jump are conditinal jumps */
	tcg_gen_mov_i32(cpu_pc, cpu_delayed_pc);
        tcg_gen_discard_i32(cpu_delayed_pc);
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else if (use_exit_tb(ctx)) {
            tcg_gen_exit_tb(NULL, 0);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
	gen_goto_tb(ctx, 0, ctx->delayed_pc);
    }
}

/* Immediate conditional jump (bt or bf) */
static void gen_conditional_jump(DisasContext *ctx, target_ulong dest,
                                 bool jump_if_true)
{
    TCGLabel *l1 = gen_new_label();
    TCGCond cond_not_taken = jump_if_true ? TCG_COND_EQ : TCG_COND_NE;

    if (ctx->tbflags & GUSA_EXCLUSIVE) {
        /* When in an exclusive region, we must continue to the end.
           Therefore, exit the region on a taken branch, but otherwise
           fall through to the next instruction.  */
        tcg_gen_brcondi_i32(cond_not_taken, cpu_sr_t, 0, l1);
        tcg_gen_movi_i32(cpu_flags, ctx->envflags & ~GUSA_MASK);
        /* Note that this won't actually use a goto_tb opcode because we
           disallow it in use_goto_tb, but it handles exit + singlestep.  */
        gen_goto_tb(ctx, 0, dest);
        gen_set_label(l1);
        ctx->base.is_jmp = DISAS_NEXT;
        return;
    }

    gen_save_cpu_state(ctx, false);
    tcg_gen_brcondi_i32(cond_not_taken, cpu_sr_t, 0, l1);
    gen_goto_tb(ctx, 0, dest);
    gen_set_label(l1);
    gen_goto_tb(ctx, 1, ctx->base.pc_next + 2);
    ctx->base.is_jmp = DISAS_NORETURN;
}

/* Delayed conditional jump (bt or bf) */
static void gen_delayed_conditional_jump(DisasContext * ctx)
{
    TCGLabel *l1 = gen_new_label();
    TCGv ds = tcg_temp_new();

    tcg_gen_mov_i32(ds, cpu_delayed_cond);
    tcg_gen_discard_i32(cpu_delayed_cond);

    if (ctx->tbflags & GUSA_EXCLUSIVE) {
        /* When in an exclusive region, we must continue to the end.
           Therefore, exit the region on a taken branch, but otherwise
           fall through to the next instruction.  */
        tcg_gen_brcondi_i32(TCG_COND_EQ, ds, 0, l1);

        /* Leave the gUSA region.  */
        tcg_gen_movi_i32(cpu_flags, ctx->envflags & ~GUSA_MASK);
        gen_jump(ctx);

        gen_set_label(l1);
        ctx->base.is_jmp = DISAS_NEXT;
        return;
    }

    tcg_gen_brcondi_i32(TCG_COND_NE, ds, 0, l1);
    gen_goto_tb(ctx, 1, ctx->base.pc_next + 2);
    gen_set_label(l1);
    gen_jump(ctx);
}

static inline void gen_load_fpr64(DisasContext *ctx, TCGv_i64 t, int reg)
{
    /* We have already signaled illegal instruction for odd Dr.  */
    tcg_debug_assert((reg & 1) == 0);
    reg ^= ctx->fbank;
    tcg_gen_concat_i32_i64(t, cpu_fregs[reg + 1], cpu_fregs[reg]);
}

static inline void gen_store_fpr64(DisasContext *ctx, TCGv_i64 t, int reg)
{
    /* We have already signaled illegal instruction for odd Dr.  */
    tcg_debug_assert((reg & 1) == 0);
    reg ^= ctx->fbank;
    tcg_gen_extr_i64_i32(cpu_fregs[reg + 1], cpu_fregs[reg], t);
}

#define B3_0 (ctx->opcode & 0xf)
#define B6_4 ((ctx->opcode >> 4) & 0x7)
#define B7_4 ((ctx->opcode >> 4) & 0xf)
#define B7_0 (ctx->opcode & 0xff)
#define B7_0s ((int32_t) (int8_t) (ctx->opcode & 0xff))
#define B11_0s (ctx->opcode & 0x800 ? 0xfffff000 | (ctx->opcode & 0xfff) : \
  (ctx->opcode & 0xfff))
#define B11_8 ((ctx->opcode >> 8) & 0xf)
#define B15_12 ((ctx->opcode >> 12) & 0xf)

#define REG(x)     cpu_gregs[(x) ^ ctx->gbank]
#define ALTREG(x)  cpu_gregs[(x) ^ ctx->gbank ^ 0x10]
#define FREG(x)    cpu_fregs[(x) ^ ctx->fbank]

#define XHACK(x) ((((x) & 1 ) << 4) | ((x) & 0xe))

#define CHECK_NOT_DELAY_SLOT \
    if (ctx->envflags & DELAY_SLOT_MASK) {  \
        goto do_illegal_slot;               \
    }

#define CHECK_PRIVILEGED \
    if (IS_USER(ctx)) {                     \
        goto do_illegal;                    \
    }

#define CHECK_FPU_ENABLED \
    if (ctx->tbflags & (1u << SR_FD)) {     \
        goto do_fpu_disabled;               \
    }

#define CHECK_FPSCR_PR_0 \
    if (ctx->tbflags & FPSCR_PR) {          \
        goto do_illegal;                    \
    }

#define CHECK_FPSCR_PR_1 \
    if (!(ctx->tbflags & FPSCR_PR)) {       \
        goto do_illegal;                    \
    }

#define CHECK_SH4A \
    if (!(ctx->features & SH_FEATURE_SH4A)) { \
        goto do_illegal;                      \
    }

static void _decode_opc(DisasContext * ctx)
{
    /* This code tries to make movcal emulation sufficiently
       accurate for Linux purposes.  This instruction writes
       memory, and prior to that, always allocates a cache line.
       It is used in two contexts:
       - in memcpy, where data is copied in blocks, the first write
       of to a block uses movca.l for performance.
       - in arch/sh/mm/cache-sh4.c, movcal.l + ocbi combination is used
       to flush the cache. Here, the data written by movcal.l is never
       written to memory, and the data written is just bogus.

       To simulate this, we simulate movcal.l, we store the value to memory,
       but we also remember the previous content. If we see ocbi, we check
       if movcal.l for that address was done previously. If so, the write should
       not have hit the memory, so we restore the previous content.
       When we see an instruction that is neither movca.l
       nor ocbi, the previous content is discarded.

       To optimize, we only try to flush stores when we're at the start of
       TB, or if we already saw movca.l in this TB and did not flush stores
       yet.  */
    if (ctx->has_movcal)
	{
	  int opcode = ctx->opcode & 0xf0ff;
	  if (opcode != 0x0093 /* ocbi */
	      && opcode != 0x00c3 /* movca.l */)
	      {
                  gen_helper_discard_movcal_backup(cpu_env);
		  ctx->has_movcal = 0;
	      }
	}

#if 0
    fprintf(stderr, "Translating opcode 0x%04x\n", ctx->opcode);
#endif

    switch (ctx->opcode) {
    case 0x0019:		/* div0u */
        tcg_gen_movi_i32(cpu_sr_m, 0);
        tcg_gen_movi_i32(cpu_sr_q, 0);
        tcg_gen_movi_i32(cpu_sr_t, 0);
	return;
    case 0x000b:		/* rts */
	CHECK_NOT_DELAY_SLOT
	tcg_gen_mov_i32(cpu_delayed_pc, cpu_pr);
        ctx->envflags |= DELAY_SLOT;
	ctx->delayed_pc = (uint32_t) - 1;
	return;
    case 0x0028:		/* clrmac */
	tcg_gen_movi_i32(cpu_mach, 0);
	tcg_gen_movi_i32(cpu_macl, 0);
	return;
    case 0x0048:		/* clrs */
        tcg_gen_andi_i32(cpu_sr, cpu_sr, ~(1u << SR_S));
	return;
    case 0x0008:		/* clrt */
        tcg_gen_movi_i32(cpu_sr_t, 0);
	return;
    case 0x0038:		/* ldtlb */
	CHECK_PRIVILEGED
        gen_helper_ldtlb(cpu_env);
	return;
    case 0x002b:		/* rte */
	CHECK_PRIVILEGED
	CHECK_NOT_DELAY_SLOT
        gen_write_sr(cpu_ssr);
	tcg_gen_mov_i32(cpu_delayed_pc, cpu_spc);
        ctx->envflags |= DELAY_SLOT_RTE;
	ctx->delayed_pc = (uint32_t) - 1;
        ctx->base.is_jmp = DISAS_STOP;
	return;
    case 0x0058:		/* sets */
        tcg_gen_ori_i32(cpu_sr, cpu_sr, (1u << SR_S));
	return;
    case 0x0018:		/* sett */
        tcg_gen_movi_i32(cpu_sr_t, 1);
	return;
    case 0xfbfd:		/* frchg */
        CHECK_FPSCR_PR_0
	tcg_gen_xori_i32(cpu_fpscr, cpu_fpscr, FPSCR_FR);
        ctx->base.is_jmp = DISAS_STOP;
	return;
    case 0xf3fd:		/* fschg */
        CHECK_FPSCR_PR_0
        tcg_gen_xori_i32(cpu_fpscr, cpu_fpscr, FPSCR_SZ);
        ctx->base.is_jmp = DISAS_STOP;
	return;
    case 0xf7fd:                /* fpchg */
        CHECK_SH4A
        tcg_gen_xori_i32(cpu_fpscr, cpu_fpscr, FPSCR_PR);
        ctx->base.is_jmp = DISAS_STOP;
        return;
    case 0x0009:		/* nop */
	return;
    case 0x001b:		/* sleep */
	CHECK_PRIVILEGED
        tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next + 2);
        gen_helper_sleep(cpu_env);
	return;
    }

    switch (ctx->opcode & 0xf000) {
    case 0x1000:		/* mov.l Rm,@(disp,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B11_8), B3_0 * 4);
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_TEUL);
	    tcg_temp_free(addr);
	}
	return;
    case 0x5000:		/* mov.l @(disp,Rm),Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B7_4), B3_0 * 4);
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_TESL);
	    tcg_temp_free(addr);
	}
	return;
    case 0xe000:		/* mov #imm,Rn */
#ifdef CONFIG_USER_ONLY
        /* Detect the start of a gUSA region.  If so, update envflags
           and end the TB.  This will allow us to see the end of the
           region (stored in R0) in the next TB.  */
        if (B11_8 == 15 && B7_0s < 0 &&
            (tb_cflags(ctx->base.tb) & CF_PARALLEL)) {
            ctx->envflags = deposit32(ctx->envflags, GUSA_SHIFT, 8, B7_0s);
            ctx->base.is_jmp = DISAS_STOP;
        }
#endif
	tcg_gen_movi_i32(REG(B11_8), B7_0s);
	return;
    case 0x9000:		/* mov.w @(disp,PC),Rn */
	{
            TCGv addr = tcg_const_i32(ctx->base.pc_next + 4 + B7_0 * 2);
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_TESW);
	    tcg_temp_free(addr);
	}
	return;
    case 0xd000:		/* mov.l @(disp,PC),Rn */
	{
            TCGv addr = tcg_const_i32((ctx->base.pc_next + 4 + B7_0 * 4) & ~3);
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_TESL);
	    tcg_temp_free(addr);
	}
	return;
    case 0x7000:		/* add #imm,Rn */
	tcg_gen_addi_i32(REG(B11_8), REG(B11_8), B7_0s);
	return;
    case 0xa000:		/* bra disp */
	CHECK_NOT_DELAY_SLOT
        ctx->delayed_pc = ctx->base.pc_next + 4 + B11_0s * 2;
        ctx->envflags |= DELAY_SLOT;
	return;
    case 0xb000:		/* bsr disp */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_movi_i32(cpu_pr, ctx->base.pc_next + 4);
        ctx->delayed_pc = ctx->base.pc_next + 4 + B11_0s * 2;
        ctx->envflags |= DELAY_SLOT;
	return;
    }

    switch (ctx->opcode & 0xf00f) {
    case 0x6003:		/* mov Rm,Rn */
	tcg_gen_mov_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x2000:		/* mov.b Rm,@Rn */
        tcg_gen_qemu_st_i32(REG(B7_4), REG(B11_8), ctx->memidx, MO_UB);
	return;
    case 0x2001:		/* mov.w Rm,@Rn */
        tcg_gen_qemu_st_i32(REG(B7_4), REG(B11_8), ctx->memidx, MO_TEUW);
	return;
    case 0x2002:		/* mov.l Rm,@Rn */
        tcg_gen_qemu_st_i32(REG(B7_4), REG(B11_8), ctx->memidx, MO_TEUL);
	return;
    case 0x6000:		/* mov.b @Rm,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_SB);
	return;
    case 0x6001:		/* mov.w @Rm,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_TESW);
	return;
    case 0x6002:		/* mov.l @Rm,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_TESL);
	return;
    case 0x2004:		/* mov.b Rm,@-Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 1);
            /* might cause re-execution */
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_UB);
	    tcg_gen_mov_i32(REG(B11_8), addr);			/* modify register status */
	    tcg_temp_free(addr);
	}
	return;
    case 0x2005:		/* mov.w Rm,@-Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 2);
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_TEUW);
	    tcg_gen_mov_i32(REG(B11_8), addr);
	    tcg_temp_free(addr);
	}
	return;
    case 0x2006:		/* mov.l Rm,@-Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 4);
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_TEUL);
	    tcg_gen_mov_i32(REG(B11_8), addr);
        tcg_temp_free(addr);
	}
	return;
    case 0x6004:		/* mov.b @Rm+,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_SB);
	if ( B11_8 != B7_4 )
		tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 1);
	return;
    case 0x6005:		/* mov.w @Rm+,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_TESW);
	if ( B11_8 != B7_4 )
		tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 2);
	return;
    case 0x6006:		/* mov.l @Rm+,Rn */
        tcg_gen_qemu_ld_i32(REG(B11_8), REG(B7_4), ctx->memidx, MO_TESL);
	if ( B11_8 != B7_4 )
		tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 4);
	return;
    case 0x0004:		/* mov.b Rm,@(R0,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B11_8), REG(0));
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_UB);
	    tcg_temp_free(addr);
	}
	return;
    case 0x0005:		/* mov.w Rm,@(R0,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B11_8), REG(0));
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_TEUW);
	    tcg_temp_free(addr);
	}
	return;
    case 0x0006:		/* mov.l Rm,@(R0,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B11_8), REG(0));
            tcg_gen_qemu_st_i32(REG(B7_4), addr, ctx->memidx, MO_TEUL);
	    tcg_temp_free(addr);
	}
	return;
    case 0x000c:		/* mov.b @(R0,Rm),Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B7_4), REG(0));
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_SB);
	    tcg_temp_free(addr);
	}
	return;
    case 0x000d:		/* mov.w @(R0,Rm),Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B7_4), REG(0));
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_TESW);
	    tcg_temp_free(addr);
	}
	return;
    case 0x000e:		/* mov.l @(R0,Rm),Rn */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B7_4), REG(0));
            tcg_gen_qemu_ld_i32(REG(B11_8), addr, ctx->memidx, MO_TESL);
	    tcg_temp_free(addr);
	}
	return;
    case 0x6008:		/* swap.b Rm,Rn */
	{
            TCGv low = tcg_temp_new();
	    tcg_gen_ext16u_i32(low, REG(B7_4));
	    tcg_gen_bswap16_i32(low, low);
            tcg_gen_deposit_i32(REG(B11_8), REG(B7_4), low, 0, 16);
	    tcg_temp_free(low);
	}
	return;
    case 0x6009:		/* swap.w Rm,Rn */
        tcg_gen_rotli_i32(REG(B11_8), REG(B7_4), 16);
	return;
    case 0x200d:		/* xtrct Rm,Rn */
	{
	    TCGv high, low;
	    high = tcg_temp_new();
	    tcg_gen_shli_i32(high, REG(B7_4), 16);
	    low = tcg_temp_new();
	    tcg_gen_shri_i32(low, REG(B11_8), 16);
	    tcg_gen_or_i32(REG(B11_8), high, low);
	    tcg_temp_free(low);
	    tcg_temp_free(high);
	}
	return;
    case 0x300c:		/* add Rm,Rn */
	tcg_gen_add_i32(REG(B11_8), REG(B11_8), REG(B7_4));
	return;
    case 0x300e:		/* addc Rm,Rn */
        {
            TCGv t0, t1;
            t0 = tcg_const_tl(0);
            t1 = tcg_temp_new();
            tcg_gen_add2_i32(t1, cpu_sr_t, cpu_sr_t, t0, REG(B7_4), t0);
            tcg_gen_add2_i32(REG(B11_8), cpu_sr_t,
                             REG(B11_8), t0, t1, cpu_sr_t);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
	return;
    case 0x300f:		/* addv Rm,Rn */
        {
            TCGv t0, t1, t2;
            t0 = tcg_temp_new();
            tcg_gen_add_i32(t0, REG(B7_4), REG(B11_8));
            t1 = tcg_temp_new();
            tcg_gen_xor_i32(t1, t0, REG(B11_8));
            t2 = tcg_temp_new();
            tcg_gen_xor_i32(t2, REG(B7_4), REG(B11_8));
            tcg_gen_andc_i32(cpu_sr_t, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_shri_i32(cpu_sr_t, cpu_sr_t, 31);
            tcg_temp_free(t1);
            tcg_gen_mov_i32(REG(B7_4), t0);
            tcg_temp_free(t0);
        }
	return;
    case 0x2009:		/* and Rm,Rn */
	tcg_gen_and_i32(REG(B11_8), REG(B11_8), REG(B7_4));
	return;
    case 0x3000:		/* cmp/eq Rm,Rn */
        tcg_gen_setcond_i32(TCG_COND_EQ, cpu_sr_t, REG(B11_8), REG(B7_4));
	return;
    case 0x3003:		/* cmp/ge Rm,Rn */
        tcg_gen_setcond_i32(TCG_COND_GE, cpu_sr_t, REG(B11_8), REG(B7_4));
	return;
    case 0x3007:		/* cmp/gt Rm,Rn */
        tcg_gen_setcond_i32(TCG_COND_GT, cpu_sr_t, REG(B11_8), REG(B7_4));
	return;
    case 0x3006:		/* cmp/hi Rm,Rn */
        tcg_gen_setcond_i32(TCG_COND_GTU, cpu_sr_t, REG(B11_8), REG(B7_4));
	return;
    case 0x3002:		/* cmp/hs Rm,Rn */
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_sr_t, REG(B11_8), REG(B7_4));
	return;
    case 0x200c:		/* cmp/str Rm,Rn */
	{
	    TCGv cmp1 = tcg_temp_new();
	    TCGv cmp2 = tcg_temp_new();
            tcg_gen_xor_i32(cmp2, REG(B7_4), REG(B11_8));
            tcg_gen_subi_i32(cmp1, cmp2, 0x01010101);
            tcg_gen_andc_i32(cmp1, cmp1, cmp2);
            tcg_gen_andi_i32(cmp1, cmp1, 0x80808080);
            tcg_gen_setcondi_i32(TCG_COND_NE, cpu_sr_t, cmp1, 0);
	    tcg_temp_free(cmp2);
	    tcg_temp_free(cmp1);
	}
	return;
    case 0x2007:		/* div0s Rm,Rn */
        tcg_gen_shri_i32(cpu_sr_q, REG(B11_8), 31);         /* SR_Q */
        tcg_gen_shri_i32(cpu_sr_m, REG(B7_4), 31);          /* SR_M */
        tcg_gen_xor_i32(cpu_sr_t, cpu_sr_q, cpu_sr_m);      /* SR_T */
	return;
    case 0x3004:		/* div1 Rm,Rn */
        {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGv zero = tcg_const_i32(0);

            /* shift left arg1, saving the bit being pushed out and inserting
               T on the right */
            tcg_gen_shri_i32(t0, REG(B11_8), 31);
            tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 1);
            tcg_gen_or_i32(REG(B11_8), REG(B11_8), cpu_sr_t);

            /* Add or subtract arg0 from arg1 depending if Q == M. To avoid
               using 64-bit temps, we compute arg0's high part from q ^ m, so
               that it is 0x00000000 when adding the value or 0xffffffff when
               subtracting it. */
            tcg_gen_xor_i32(t1, cpu_sr_q, cpu_sr_m);
            tcg_gen_subi_i32(t1, t1, 1);
            tcg_gen_neg_i32(t2, REG(B7_4));
            tcg_gen_movcond_i32(TCG_COND_EQ, t2, t1, zero, REG(B7_4), t2);
            tcg_gen_add2_i32(REG(B11_8), t1, REG(B11_8), zero, t2, t1);

            /* compute T and Q depending on carry */
            tcg_gen_andi_i32(t1, t1, 1);
            tcg_gen_xor_i32(t1, t1, t0);
            tcg_gen_xori_i32(cpu_sr_t, t1, 1);
            tcg_gen_xor_i32(cpu_sr_q, cpu_sr_m, t1);

            tcg_temp_free(zero);
            tcg_temp_free(t2);
            tcg_temp_free(t1);
            tcg_temp_free(t0);
        }
	return;
    case 0x300d:		/* dmuls.l Rm,Rn */
        tcg_gen_muls2_i32(cpu_macl, cpu_mach, REG(B7_4), REG(B11_8));
	return;
    case 0x3005:		/* dmulu.l Rm,Rn */
        tcg_gen_mulu2_i32(cpu_macl, cpu_mach, REG(B7_4), REG(B11_8));
	return;
    case 0x600e:		/* exts.b Rm,Rn */
	tcg_gen_ext8s_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x600f:		/* exts.w Rm,Rn */
	tcg_gen_ext16s_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x600c:		/* extu.b Rm,Rn */
	tcg_gen_ext8u_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x600d:		/* extu.w Rm,Rn */
	tcg_gen_ext16u_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x000f:		/* mac.l @Rm+,@Rn+ */
	{
	    TCGv arg0, arg1;
	    arg0 = tcg_temp_new();
            tcg_gen_qemu_ld_i32(arg0, REG(B7_4), ctx->memidx, MO_TESL);
	    arg1 = tcg_temp_new();
            tcg_gen_qemu_ld_i32(arg1, REG(B11_8), ctx->memidx, MO_TESL);
            gen_helper_macl(cpu_env, arg0, arg1);
	    tcg_temp_free(arg1);
	    tcg_temp_free(arg0);
	    tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 4);
	    tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);
	}
	return;
    case 0x400f:		/* mac.w @Rm+,@Rn+ */
	{
	    TCGv arg0, arg1;
	    arg0 = tcg_temp_new();
            tcg_gen_qemu_ld_i32(arg0, REG(B7_4), ctx->memidx, MO_TESL);
	    arg1 = tcg_temp_new();
            tcg_gen_qemu_ld_i32(arg1, REG(B11_8), ctx->memidx, MO_TESL);
            gen_helper_macw(cpu_env, arg0, arg1);
	    tcg_temp_free(arg1);
	    tcg_temp_free(arg0);
	    tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 2);
	    tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 2);
	}
	return;
    case 0x0007:		/* mul.l Rm,Rn */
	tcg_gen_mul_i32(cpu_macl, REG(B7_4), REG(B11_8));
	return;
    case 0x200f:		/* muls.w Rm,Rn */
	{
	    TCGv arg0, arg1;
	    arg0 = tcg_temp_new();
	    tcg_gen_ext16s_i32(arg0, REG(B7_4));
	    arg1 = tcg_temp_new();
	    tcg_gen_ext16s_i32(arg1, REG(B11_8));
	    tcg_gen_mul_i32(cpu_macl, arg0, arg1);
	    tcg_temp_free(arg1);
	    tcg_temp_free(arg0);
	}
	return;
    case 0x200e:		/* mulu.w Rm,Rn */
	{
	    TCGv arg0, arg1;
	    arg0 = tcg_temp_new();
	    tcg_gen_ext16u_i32(arg0, REG(B7_4));
	    arg1 = tcg_temp_new();
	    tcg_gen_ext16u_i32(arg1, REG(B11_8));
	    tcg_gen_mul_i32(cpu_macl, arg0, arg1);
	    tcg_temp_free(arg1);
	    tcg_temp_free(arg0);
	}
	return;
    case 0x600b:		/* neg Rm,Rn */
	tcg_gen_neg_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x600a:		/* negc Rm,Rn */
        {
            TCGv t0 = tcg_const_i32(0);
            tcg_gen_add2_i32(REG(B11_8), cpu_sr_t,
                             REG(B7_4), t0, cpu_sr_t, t0);
            tcg_gen_sub2_i32(REG(B11_8), cpu_sr_t,
                             t0, t0, REG(B11_8), cpu_sr_t);
            tcg_gen_andi_i32(cpu_sr_t, cpu_sr_t, 1);
            tcg_temp_free(t0);
        }
	return;
    case 0x6007:		/* not Rm,Rn */
	tcg_gen_not_i32(REG(B11_8), REG(B7_4));
	return;
    case 0x200b:		/* or Rm,Rn */
	tcg_gen_or_i32(REG(B11_8), REG(B11_8), REG(B7_4));
	return;
    case 0x400c:		/* shad Rm,Rn */
	{
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();

            tcg_gen_andi_i32(t0, REG(B7_4), 0x1f);

            /* positive case: shift to the left */
            tcg_gen_shl_i32(t1, REG(B11_8), t0);

            /* negative case: shift to the right in two steps to
               correctly handle the -32 case */
            tcg_gen_xori_i32(t0, t0, 0x1f);
            tcg_gen_sar_i32(t2, REG(B11_8), t0);
            tcg_gen_sari_i32(t2, t2, 1);

            /* select between the two cases */
            tcg_gen_movi_i32(t0, 0);
            tcg_gen_movcond_i32(TCG_COND_GE, REG(B11_8), REG(B7_4), t0, t1, t2);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
            tcg_temp_free(t2);
	}
	return;
    case 0x400d:		/* shld Rm,Rn */
	{
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();

            tcg_gen_andi_i32(t0, REG(B7_4), 0x1f);

            /* positive case: shift to the left */
            tcg_gen_shl_i32(t1, REG(B11_8), t0);

            /* negative case: shift to the right in two steps to
               correctly handle the -32 case */
            tcg_gen_xori_i32(t0, t0, 0x1f);
            tcg_gen_shr_i32(t2, REG(B11_8), t0);
            tcg_gen_shri_i32(t2, t2, 1);

            /* select between the two cases */
            tcg_gen_movi_i32(t0, 0);
            tcg_gen_movcond_i32(TCG_COND_GE, REG(B11_8), REG(B7_4), t0, t1, t2);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
            tcg_temp_free(t2);
	}
	return;
    case 0x3008:		/* sub Rm,Rn */
	tcg_gen_sub_i32(REG(B11_8), REG(B11_8), REG(B7_4));
	return;
    case 0x300a:		/* subc Rm,Rn */
        {
            TCGv t0, t1;
            t0 = tcg_const_tl(0);
            t1 = tcg_temp_new();
            tcg_gen_add2_i32(t1, cpu_sr_t, cpu_sr_t, t0, REG(B7_4), t0);
            tcg_gen_sub2_i32(REG(B11_8), cpu_sr_t,
                             REG(B11_8), t0, t1, cpu_sr_t);
            tcg_gen_andi_i32(cpu_sr_t, cpu_sr_t, 1);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
	return;
    case 0x300b:		/* subv Rm,Rn */
        {
            TCGv t0, t1, t2;
            t0 = tcg_temp_new();
            tcg_gen_sub_i32(t0, REG(B11_8), REG(B7_4));
            t1 = tcg_temp_new();
            tcg_gen_xor_i32(t1, t0, REG(B7_4));
            t2 = tcg_temp_new();
            tcg_gen_xor_i32(t2, REG(B11_8), REG(B7_4));
            tcg_gen_and_i32(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_shri_i32(cpu_sr_t, t1, 31);
            tcg_temp_free(t1);
            tcg_gen_mov_i32(REG(B11_8), t0);
            tcg_temp_free(t0);
        }
	return;
    case 0x2008:		/* tst Rm,Rn */
	{
	    TCGv val = tcg_temp_new();
	    tcg_gen_and_i32(val, REG(B7_4), REG(B11_8));
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, val, 0);
	    tcg_temp_free(val);
	}
	return;
    case 0x200a:		/* xor Rm,Rn */
	tcg_gen_xor_i32(REG(B11_8), REG(B11_8), REG(B7_4));
	return;
    case 0xf00c: /* fmov {F,D,X}Rm,{F,D,X}Rn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_SZ) {
            int xsrc = XHACK(B7_4);
            int xdst = XHACK(B11_8);
            tcg_gen_mov_i32(FREG(xdst), FREG(xsrc));
            tcg_gen_mov_i32(FREG(xdst + 1), FREG(xsrc + 1));
	} else {
            tcg_gen_mov_i32(FREG(B11_8), FREG(B7_4));
	}
	return;
    case 0xf00a: /* fmov {F,D,X}Rm,@Rn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_SZ) {
            TCGv_i64 fp = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp, XHACK(B7_4));
            tcg_gen_qemu_st_i64(fp, REG(B11_8), ctx->memidx, MO_TEQ);
            tcg_temp_free_i64(fp);
	} else {
            tcg_gen_qemu_st_i32(FREG(B7_4), REG(B11_8), ctx->memidx, MO_TEUL);
	}
	return;
    case 0xf008: /* fmov @Rm,{F,D,X}Rn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_SZ) {
            TCGv_i64 fp = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(fp, REG(B7_4), ctx->memidx, MO_TEQ);
            gen_store_fpr64(ctx, fp, XHACK(B11_8));
            tcg_temp_free_i64(fp);
	} else {
            tcg_gen_qemu_ld_i32(FREG(B11_8), REG(B7_4), ctx->memidx, MO_TEUL);
	}
	return;
    case 0xf009: /* fmov @Rm+,{F,D,X}Rn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_SZ) {
            TCGv_i64 fp = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(fp, REG(B7_4), ctx->memidx, MO_TEQ);
            gen_store_fpr64(ctx, fp, XHACK(B11_8));
            tcg_temp_free_i64(fp);
            tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 8);
	} else {
            tcg_gen_qemu_ld_i32(FREG(B11_8), REG(B7_4), ctx->memidx, MO_TEUL);
	    tcg_gen_addi_i32(REG(B7_4), REG(B7_4), 4);
	}
	return;
    case 0xf00b: /* fmov {F,D,X}Rm,@-Rn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        {
            TCGv addr = tcg_temp_new_i32();
            if (ctx->tbflags & FPSCR_SZ) {
                TCGv_i64 fp = tcg_temp_new_i64();
                gen_load_fpr64(ctx, fp, XHACK(B7_4));
                tcg_gen_subi_i32(addr, REG(B11_8), 8);
                tcg_gen_qemu_st_i64(fp, addr, ctx->memidx, MO_TEQ);
                tcg_temp_free_i64(fp);
            } else {
                tcg_gen_subi_i32(addr, REG(B11_8), 4);
                tcg_gen_qemu_st_i32(FREG(B7_4), addr, ctx->memidx, MO_TEUL);
            }
            tcg_gen_mov_i32(REG(B11_8), addr);
            tcg_temp_free(addr);
        }
	return;
    case 0xf006: /* fmov @(R0,Rm),{F,D,X}Rm - FPSCR: Nothing */
	CHECK_FPU_ENABLED
	{
	    TCGv addr = tcg_temp_new_i32();
	    tcg_gen_add_i32(addr, REG(B7_4), REG(0));
            if (ctx->tbflags & FPSCR_SZ) {
                TCGv_i64 fp = tcg_temp_new_i64();
                tcg_gen_qemu_ld_i64(fp, addr, ctx->memidx, MO_TEQ);
                gen_store_fpr64(ctx, fp, XHACK(B11_8));
                tcg_temp_free_i64(fp);
	    } else {
                tcg_gen_qemu_ld_i32(FREG(B11_8), addr, ctx->memidx, MO_TEUL);
	    }
	    tcg_temp_free(addr);
	}
	return;
    case 0xf007: /* fmov {F,D,X}Rn,@(R0,Rn) - FPSCR: Nothing */
	CHECK_FPU_ENABLED
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(B11_8), REG(0));
            if (ctx->tbflags & FPSCR_SZ) {
                TCGv_i64 fp = tcg_temp_new_i64();
                gen_load_fpr64(ctx, fp, XHACK(B7_4));
                tcg_gen_qemu_st_i64(fp, addr, ctx->memidx, MO_TEQ);
                tcg_temp_free_i64(fp);
	    } else {
                tcg_gen_qemu_st_i32(FREG(B7_4), addr, ctx->memidx, MO_TEUL);
	    }
	    tcg_temp_free(addr);
	}
	return;
    case 0xf000: /* fadd Rm,Rn - FPSCR: R[PR,Enable.O/U/I]/W[Cause,Flag] */
    case 0xf001: /* fsub Rm,Rn - FPSCR: R[PR,Enable.O/U/I]/W[Cause,Flag] */
    case 0xf002: /* fmul Rm,Rn - FPSCR: R[PR,Enable.O/U/I]/W[Cause,Flag] */
    case 0xf003: /* fdiv Rm,Rn - FPSCR: R[PR,Enable.O/U/I]/W[Cause,Flag] */
    case 0xf004: /* fcmp/eq Rm,Rn - FPSCR: R[PR,Enable.V]/W[Cause,Flag] */
    case 0xf005: /* fcmp/gt Rm,Rn - FPSCR: R[PR,Enable.V]/W[Cause,Flag] */
	{
	    CHECK_FPU_ENABLED
            if (ctx->tbflags & FPSCR_PR) {
                TCGv_i64 fp0, fp1;

                if (ctx->opcode & 0x0110) {
                    goto do_illegal;
                }
		fp0 = tcg_temp_new_i64();
		fp1 = tcg_temp_new_i64();
                gen_load_fpr64(ctx, fp0, B11_8);
                gen_load_fpr64(ctx, fp1, B7_4);
                switch (ctx->opcode & 0xf00f) {
                case 0xf000:		/* fadd Rm,Rn */
                    gen_helper_fadd_DT(fp0, cpu_env, fp0, fp1);
                    break;
                case 0xf001:		/* fsub Rm,Rn */
                    gen_helper_fsub_DT(fp0, cpu_env, fp0, fp1);
                    break;
                case 0xf002:		/* fmul Rm,Rn */
                    gen_helper_fmul_DT(fp0, cpu_env, fp0, fp1);
                    break;
                case 0xf003:		/* fdiv Rm,Rn */
                    gen_helper_fdiv_DT(fp0, cpu_env, fp0, fp1);
                    break;
                case 0xf004:		/* fcmp/eq Rm,Rn */
                    gen_helper_fcmp_eq_DT(cpu_sr_t, cpu_env, fp0, fp1);
                    return;
                case 0xf005:		/* fcmp/gt Rm,Rn */
                    gen_helper_fcmp_gt_DT(cpu_sr_t, cpu_env, fp0, fp1);
                    return;
                }
                gen_store_fpr64(ctx, fp0, B11_8);
                tcg_temp_free_i64(fp0);
                tcg_temp_free_i64(fp1);
	    } else {
                switch (ctx->opcode & 0xf00f) {
                case 0xf000:		/* fadd Rm,Rn */
                    gen_helper_fadd_FT(FREG(B11_8), cpu_env,
                                       FREG(B11_8), FREG(B7_4));
                    break;
                case 0xf001:		/* fsub Rm,Rn */
                    gen_helper_fsub_FT(FREG(B11_8), cpu_env,
                                       FREG(B11_8), FREG(B7_4));
                    break;
                case 0xf002:		/* fmul Rm,Rn */
                    gen_helper_fmul_FT(FREG(B11_8), cpu_env,
                                       FREG(B11_8), FREG(B7_4));
                    break;
                case 0xf003:		/* fdiv Rm,Rn */
                    gen_helper_fdiv_FT(FREG(B11_8), cpu_env,
                                       FREG(B11_8), FREG(B7_4));
                    break;
                case 0xf004:		/* fcmp/eq Rm,Rn */
                    gen_helper_fcmp_eq_FT(cpu_sr_t, cpu_env,
                                          FREG(B11_8), FREG(B7_4));
                    return;
                case 0xf005:		/* fcmp/gt Rm,Rn */
                    gen_helper_fcmp_gt_FT(cpu_sr_t, cpu_env,
                                          FREG(B11_8), FREG(B7_4));
                    return;
                }
	    }
	}
	return;
    case 0xf00e: /* fmac FR0,RM,Rn */
        CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_0
        gen_helper_fmac_FT(FREG(B11_8), cpu_env,
                           FREG(0), FREG(B7_4), FREG(B11_8));
        return;
    }

    switch (ctx->opcode & 0xff00) {
    case 0xc900:		/* and #imm,R0 */
	tcg_gen_andi_i32(REG(0), REG(0), B7_0);
	return;
    case 0xcd00:		/* and.b #imm,@(R0,GBR) */
	{
	    TCGv addr, val;
	    addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(0), cpu_gbr);
	    val = tcg_temp_new();
            tcg_gen_qemu_ld_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_gen_andi_i32(val, val, B7_0);
            tcg_gen_qemu_st_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_temp_free(val);
	    tcg_temp_free(addr);
	}
	return;
    case 0x8b00:		/* bf label */
	CHECK_NOT_DELAY_SLOT
        gen_conditional_jump(ctx, ctx->base.pc_next + 4 + B7_0s * 2, false);
	return;
    case 0x8f00:		/* bf/s label */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_xori_i32(cpu_delayed_cond, cpu_sr_t, 1);
        ctx->delayed_pc = ctx->base.pc_next + 4 + B7_0s * 2;
        ctx->envflags |= DELAY_SLOT_CONDITIONAL;
	return;
    case 0x8900:		/* bt label */
	CHECK_NOT_DELAY_SLOT
        gen_conditional_jump(ctx, ctx->base.pc_next + 4 + B7_0s * 2, true);
	return;
    case 0x8d00:		/* bt/s label */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_mov_i32(cpu_delayed_cond, cpu_sr_t);
        ctx->delayed_pc = ctx->base.pc_next + 4 + B7_0s * 2;
        ctx->envflags |= DELAY_SLOT_CONDITIONAL;
	return;
    case 0x8800:		/* cmp/eq #imm,R0 */
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, REG(0), B7_0s);
	return;
    case 0xc400:		/* mov.b @(disp,GBR),R0 */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0);
            tcg_gen_qemu_ld_i32(REG(0), addr, ctx->memidx, MO_SB);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc500:		/* mov.w @(disp,GBR),R0 */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0 * 2);
            tcg_gen_qemu_ld_i32(REG(0), addr, ctx->memidx, MO_TESW);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc600:		/* mov.l @(disp,GBR),R0 */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0 * 4);
            tcg_gen_qemu_ld_i32(REG(0), addr, ctx->memidx, MO_TESL);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc000:		/* mov.b R0,@(disp,GBR) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0);
            tcg_gen_qemu_st_i32(REG(0), addr, ctx->memidx, MO_UB);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc100:		/* mov.w R0,@(disp,GBR) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0 * 2);
            tcg_gen_qemu_st_i32(REG(0), addr, ctx->memidx, MO_TEUW);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc200:		/* mov.l R0,@(disp,GBR) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, cpu_gbr, B7_0 * 4);
            tcg_gen_qemu_st_i32(REG(0), addr, ctx->memidx, MO_TEUL);
	    tcg_temp_free(addr);
	}
	return;
    case 0x8000:		/* mov.b R0,@(disp,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B7_4), B3_0);
            tcg_gen_qemu_st_i32(REG(0), addr, ctx->memidx, MO_UB);
	    tcg_temp_free(addr);
	}
	return;
    case 0x8100:		/* mov.w R0,@(disp,Rn) */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B7_4), B3_0 * 2);
            tcg_gen_qemu_st_i32(REG(0), addr, ctx->memidx, MO_TEUW);
	    tcg_temp_free(addr);
	}
	return;
    case 0x8400:		/* mov.b @(disp,Rn),R0 */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B7_4), B3_0);
            tcg_gen_qemu_ld_i32(REG(0), addr, ctx->memidx, MO_SB);
	    tcg_temp_free(addr);
	}
	return;
    case 0x8500:		/* mov.w @(disp,Rn),R0 */
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_addi_i32(addr, REG(B7_4), B3_0 * 2);
            tcg_gen_qemu_ld_i32(REG(0), addr, ctx->memidx, MO_TESW);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc700:		/* mova @(disp,PC),R0 */
        tcg_gen_movi_i32(REG(0), ((ctx->base.pc_next & 0xfffffffc) +
                                  4 + B7_0 * 4) & ~3);
	return;
    case 0xcb00:		/* or #imm,R0 */
	tcg_gen_ori_i32(REG(0), REG(0), B7_0);
	return;
    case 0xcf00:		/* or.b #imm,@(R0,GBR) */
	{
	    TCGv addr, val;
	    addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(0), cpu_gbr);
	    val = tcg_temp_new();
            tcg_gen_qemu_ld_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_gen_ori_i32(val, val, B7_0);
            tcg_gen_qemu_st_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_temp_free(val);
	    tcg_temp_free(addr);
	}
	return;
    case 0xc300:		/* trapa #imm */
	{
	    TCGv imm;
	    CHECK_NOT_DELAY_SLOT
            gen_save_cpu_state(ctx, true);
	    imm = tcg_const_i32(B7_0);
            gen_helper_trapa(cpu_env, imm);
	    tcg_temp_free(imm);
            ctx->base.is_jmp = DISAS_NORETURN;
	}
	return;
    case 0xc800:		/* tst #imm,R0 */
	{
	    TCGv val = tcg_temp_new();
	    tcg_gen_andi_i32(val, REG(0), B7_0);
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, val, 0);
	    tcg_temp_free(val);
	}
	return;
    case 0xcc00:		/* tst.b #imm,@(R0,GBR) */
	{
	    TCGv val = tcg_temp_new();
	    tcg_gen_add_i32(val, REG(0), cpu_gbr);
            tcg_gen_qemu_ld_i32(val, val, ctx->memidx, MO_UB);
	    tcg_gen_andi_i32(val, val, B7_0);
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, val, 0);
	    tcg_temp_free(val);
	}
	return;
    case 0xca00:		/* xor #imm,R0 */
	tcg_gen_xori_i32(REG(0), REG(0), B7_0);
	return;
    case 0xce00:		/* xor.b #imm,@(R0,GBR) */
	{
	    TCGv addr, val;
	    addr = tcg_temp_new();
	    tcg_gen_add_i32(addr, REG(0), cpu_gbr);
	    val = tcg_temp_new();
            tcg_gen_qemu_ld_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_gen_xori_i32(val, val, B7_0);
            tcg_gen_qemu_st_i32(val, addr, ctx->memidx, MO_UB);
	    tcg_temp_free(val);
	    tcg_temp_free(addr);
	}
	return;
    }

    switch (ctx->opcode & 0xf08f) {
    case 0x408e:		/* ldc Rm,Rn_BANK */
	CHECK_PRIVILEGED
	tcg_gen_mov_i32(ALTREG(B6_4), REG(B11_8));
	return;
    case 0x4087:		/* ldc.l @Rm+,Rn_BANK */
	CHECK_PRIVILEGED
        tcg_gen_qemu_ld_i32(ALTREG(B6_4), REG(B11_8), ctx->memidx, MO_TESL);
	tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);
	return;
    case 0x0082:		/* stc Rm_BANK,Rn */
	CHECK_PRIVILEGED
	tcg_gen_mov_i32(REG(B11_8), ALTREG(B6_4));
	return;
    case 0x4083:		/* stc.l Rm_BANK,@-Rn */
	CHECK_PRIVILEGED
	{
	    TCGv addr = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 4);
            tcg_gen_qemu_st_i32(ALTREG(B6_4), addr, ctx->memidx, MO_TEUL);
	    tcg_gen_mov_i32(REG(B11_8), addr);
	    tcg_temp_free(addr);
	}
	return;
    }

    switch (ctx->opcode & 0xf0ff) {
    case 0x0023:		/* braf Rn */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_addi_i32(cpu_delayed_pc, REG(B11_8), ctx->base.pc_next + 4);
        ctx->envflags |= DELAY_SLOT;
	ctx->delayed_pc = (uint32_t) - 1;
	return;
    case 0x0003:		/* bsrf Rn */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_movi_i32(cpu_pr, ctx->base.pc_next + 4);
	tcg_gen_add_i32(cpu_delayed_pc, REG(B11_8), cpu_pr);
        ctx->envflags |= DELAY_SLOT;
	ctx->delayed_pc = (uint32_t) - 1;
	return;
    case 0x4015:		/* cmp/pl Rn */
        tcg_gen_setcondi_i32(TCG_COND_GT, cpu_sr_t, REG(B11_8), 0);
	return;
    case 0x4011:		/* cmp/pz Rn */
        tcg_gen_setcondi_i32(TCG_COND_GE, cpu_sr_t, REG(B11_8), 0);
	return;
    case 0x4010:		/* dt Rn */
	tcg_gen_subi_i32(REG(B11_8), REG(B11_8), 1);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, REG(B11_8), 0);
	return;
    case 0x402b:		/* jmp @Rn */
	CHECK_NOT_DELAY_SLOT
	tcg_gen_mov_i32(cpu_delayed_pc, REG(B11_8));
        ctx->envflags |= DELAY_SLOT;
	ctx->delayed_pc = (uint32_t) - 1;
	return;
    case 0x400b:		/* jsr @Rn */
	CHECK_NOT_DELAY_SLOT
        tcg_gen_movi_i32(cpu_pr, ctx->base.pc_next + 4);
	tcg_gen_mov_i32(cpu_delayed_pc, REG(B11_8));
        ctx->envflags |= DELAY_SLOT;
	ctx->delayed_pc = (uint32_t) - 1;
	return;
    case 0x400e:		/* ldc Rm,SR */
	CHECK_PRIVILEGED
        {
            TCGv val = tcg_temp_new();
            tcg_gen_andi_i32(val, REG(B11_8), 0x700083f3);
            gen_write_sr(val);
            tcg_temp_free(val);
            ctx->base.is_jmp = DISAS_STOP;
        }
	return;
    case 0x4007:		/* ldc.l @Rm+,SR */
	CHECK_PRIVILEGED
	{
	    TCGv val = tcg_temp_new();
            tcg_gen_qemu_ld_i32(val, REG(B11_8), ctx->memidx, MO_TESL);
            tcg_gen_andi_i32(val, val, 0x700083f3);
            gen_write_sr(val);
	    tcg_temp_free(val);
	    tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);
            ctx->base.is_jmp = DISAS_STOP;
	}
	return;
    case 0x0002:		/* stc SR,Rn */
	CHECK_PRIVILEGED
        gen_read_sr(REG(B11_8));
	return;
    case 0x4003:		/* stc SR,@-Rn */
	CHECK_PRIVILEGED
	{
	    TCGv addr = tcg_temp_new();
            TCGv val = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 4);
            gen_read_sr(val);
            tcg_gen_qemu_st_i32(val, addr, ctx->memidx, MO_TEUL);
	    tcg_gen_mov_i32(REG(B11_8), addr);
            tcg_temp_free(val);
	    tcg_temp_free(addr);
	}
	return;
#define LD(reg,ldnum,ldpnum,prechk)		\
  case ldnum:							\
    prechk    							\
    tcg_gen_mov_i32 (cpu_##reg, REG(B11_8));			\
    return;							\
  case ldpnum:							\
    prechk    							\
    tcg_gen_qemu_ld_i32(cpu_##reg, REG(B11_8), ctx->memidx, MO_TESL); \
    tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);		\
    return;
#define ST(reg,stnum,stpnum,prechk)		\
  case stnum:							\
    prechk    							\
    tcg_gen_mov_i32 (REG(B11_8), cpu_##reg);			\
    return;							\
  case stpnum:							\
    prechk    							\
    {								\
	TCGv addr = tcg_temp_new();				\
	tcg_gen_subi_i32(addr, REG(B11_8), 4);			\
        tcg_gen_qemu_st_i32(cpu_##reg, addr, ctx->memidx, MO_TEUL); \
	tcg_gen_mov_i32(REG(B11_8), addr);			\
	tcg_temp_free(addr);					\
    }								\
    return;
#define LDST(reg,ldnum,ldpnum,stnum,stpnum,prechk)		\
	LD(reg,ldnum,ldpnum,prechk)				\
	ST(reg,stnum,stpnum,prechk)
	LDST(gbr,  0x401e, 0x4017, 0x0012, 0x4013, {})
	LDST(vbr,  0x402e, 0x4027, 0x0022, 0x4023, CHECK_PRIVILEGED)
	LDST(ssr,  0x403e, 0x4037, 0x0032, 0x4033, CHECK_PRIVILEGED)
	LDST(spc,  0x404e, 0x4047, 0x0042, 0x4043, CHECK_PRIVILEGED)
	ST(sgr,  0x003a, 0x4032, CHECK_PRIVILEGED)
        LD(sgr,  0x403a, 0x4036, CHECK_PRIVILEGED CHECK_SH4A)
	LDST(dbr,  0x40fa, 0x40f6, 0x00fa, 0x40f2, CHECK_PRIVILEGED)
	LDST(mach, 0x400a, 0x4006, 0x000a, 0x4002, {})
	LDST(macl, 0x401a, 0x4016, 0x001a, 0x4012, {})
	LDST(pr,   0x402a, 0x4026, 0x002a, 0x4022, {})
	LDST(fpul, 0x405a, 0x4056, 0x005a, 0x4052, {CHECK_FPU_ENABLED})
    case 0x406a:		/* lds Rm,FPSCR */
	CHECK_FPU_ENABLED
        gen_helper_ld_fpscr(cpu_env, REG(B11_8));
        ctx->base.is_jmp = DISAS_STOP;
	return;
    case 0x4066:		/* lds.l @Rm+,FPSCR */
	CHECK_FPU_ENABLED
	{
	    TCGv addr = tcg_temp_new();
            tcg_gen_qemu_ld_i32(addr, REG(B11_8), ctx->memidx, MO_TESL);
	    tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);
            gen_helper_ld_fpscr(cpu_env, addr);
	    tcg_temp_free(addr);
            ctx->base.is_jmp = DISAS_STOP;
	}
	return;
    case 0x006a:		/* sts FPSCR,Rn */
	CHECK_FPU_ENABLED
	tcg_gen_andi_i32(REG(B11_8), cpu_fpscr, 0x003fffff);
	return;
    case 0x4062:		/* sts FPSCR,@-Rn */
	CHECK_FPU_ENABLED
	{
	    TCGv addr, val;
	    val = tcg_temp_new();
	    tcg_gen_andi_i32(val, cpu_fpscr, 0x003fffff);
	    addr = tcg_temp_new();
	    tcg_gen_subi_i32(addr, REG(B11_8), 4);
            tcg_gen_qemu_st_i32(val, addr, ctx->memidx, MO_TEUL);
	    tcg_gen_mov_i32(REG(B11_8), addr);
	    tcg_temp_free(addr);
	    tcg_temp_free(val);
	}
	return;
    case 0x00c3:		/* movca.l R0,@Rm */
        {
            TCGv val = tcg_temp_new();
            tcg_gen_qemu_ld_i32(val, REG(B11_8), ctx->memidx, MO_TEUL);
            gen_helper_movcal(cpu_env, REG(B11_8), val);
            tcg_gen_qemu_st_i32(REG(0), REG(B11_8), ctx->memidx, MO_TEUL);
            tcg_temp_free(val);
        }
        ctx->has_movcal = 1;
	return;
    case 0x40a9:                /* movua.l @Rm,R0 */
        CHECK_SH4A
        /* Load non-boundary-aligned data */
        tcg_gen_qemu_ld_i32(REG(0), REG(B11_8), ctx->memidx,
                            MO_TEUL | MO_UNALN);
        return;
        break;
    case 0x40e9:                /* movua.l @Rm+,R0 */
        CHECK_SH4A
        /* Load non-boundary-aligned data */
        tcg_gen_qemu_ld_i32(REG(0), REG(B11_8), ctx->memidx,
                            MO_TEUL | MO_UNALN);
        tcg_gen_addi_i32(REG(B11_8), REG(B11_8), 4);
        return;
        break;
    case 0x0029:		/* movt Rn */
        tcg_gen_mov_i32(REG(B11_8), cpu_sr_t);
	return;
    case 0x0073:
        /* MOVCO.L
         *     LDST -> T
         *     If (T == 1) R0 -> (Rn)
         *     0 -> LDST
         *
         * The above description doesn't work in a parallel context.
         * Since we currently support no smp boards, this implies user-mode.
         * But we can still support the official mechanism while user-mode
         * is single-threaded.  */
        CHECK_SH4A
        {
            TCGLabel *fail = gen_new_label();
            TCGLabel *done = gen_new_label();

            if ((tb_cflags(ctx->base.tb) & CF_PARALLEL)) {
                TCGv tmp;

                tcg_gen_brcond_i32(TCG_COND_NE, REG(B11_8),
                                   cpu_lock_addr, fail);
                tmp = tcg_temp_new();
                tcg_gen_atomic_cmpxchg_i32(tmp, REG(B11_8), cpu_lock_value,
                                           REG(0), ctx->memidx, MO_TEUL);
                tcg_gen_setcond_i32(TCG_COND_EQ, cpu_sr_t, tmp, cpu_lock_value);
                tcg_temp_free(tmp);
            } else {
                tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_lock_addr, -1, fail);
                tcg_gen_qemu_st_i32(REG(0), REG(B11_8), ctx->memidx, MO_TEUL);
                tcg_gen_movi_i32(cpu_sr_t, 1);
            }
            tcg_gen_br(done);

            gen_set_label(fail);
            tcg_gen_movi_i32(cpu_sr_t, 0);

            gen_set_label(done);
            tcg_gen_movi_i32(cpu_lock_addr, -1);
        }
        return;
    case 0x0063:
        /* MOVLI.L @Rm,R0
         *     1 -> LDST
         *     (Rm) -> R0
         *     When interrupt/exception
         *     occurred 0 -> LDST
         *
         * In a parallel context, we must also save the loaded value
         * for use with the cmpxchg that we'll use with movco.l.  */
        CHECK_SH4A
        if ((tb_cflags(ctx->base.tb) & CF_PARALLEL)) {
            TCGv tmp = tcg_temp_new();
            tcg_gen_mov_i32(tmp, REG(B11_8));
            tcg_gen_qemu_ld_i32(REG(0), REG(B11_8), ctx->memidx, MO_TESL);
            tcg_gen_mov_i32(cpu_lock_value, REG(0));
            tcg_gen_mov_i32(cpu_lock_addr, tmp);
            tcg_temp_free(tmp);
        } else {
            tcg_gen_qemu_ld_i32(REG(0), REG(B11_8), ctx->memidx, MO_TESL);
            tcg_gen_movi_i32(cpu_lock_addr, 0);
        }
        return;
    case 0x0093:		/* ocbi @Rn */
	{
            gen_helper_ocbi(cpu_env, REG(B11_8));
	}
	return;
    case 0x00a3:		/* ocbp @Rn */
    case 0x00b3:		/* ocbwb @Rn */
        /* These instructions are supposed to do nothing in case of
           a cache miss. Given that we only partially emulate caches
           it is safe to simply ignore them. */
	return;
    case 0x0083:		/* pref @Rn */
	return;
    case 0x00d3:		/* prefi @Rn */
        CHECK_SH4A
        return;
    case 0x00e3:		/* icbi @Rn */
        CHECK_SH4A
        return;
    case 0x00ab:		/* synco */
        CHECK_SH4A
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        return;
        break;
    case 0x4024:		/* rotcl Rn */
	{
	    TCGv tmp = tcg_temp_new();
            tcg_gen_mov_i32(tmp, cpu_sr_t);
            tcg_gen_shri_i32(cpu_sr_t, REG(B11_8), 31);
	    tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 1);
            tcg_gen_or_i32(REG(B11_8), REG(B11_8), tmp);
	    tcg_temp_free(tmp);
	}
	return;
    case 0x4025:		/* rotcr Rn */
	{
	    TCGv tmp = tcg_temp_new();
            tcg_gen_shli_i32(tmp, cpu_sr_t, 31);
            tcg_gen_andi_i32(cpu_sr_t, REG(B11_8), 1);
	    tcg_gen_shri_i32(REG(B11_8), REG(B11_8), 1);
            tcg_gen_or_i32(REG(B11_8), REG(B11_8), tmp);
	    tcg_temp_free(tmp);
	}
	return;
    case 0x4004:		/* rotl Rn */
	tcg_gen_rotli_i32(REG(B11_8), REG(B11_8), 1);
        tcg_gen_andi_i32(cpu_sr_t, REG(B11_8), 0);
	return;
    case 0x4005:		/* rotr Rn */
        tcg_gen_andi_i32(cpu_sr_t, REG(B11_8), 0);
	tcg_gen_rotri_i32(REG(B11_8), REG(B11_8), 1);
	return;
    case 0x4000:		/* shll Rn */
    case 0x4020:		/* shal Rn */
        tcg_gen_shri_i32(cpu_sr_t, REG(B11_8), 31);
	tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 1);
	return;
    case 0x4021:		/* shar Rn */
        tcg_gen_andi_i32(cpu_sr_t, REG(B11_8), 1);
	tcg_gen_sari_i32(REG(B11_8), REG(B11_8), 1);
	return;
    case 0x4001:		/* shlr Rn */
        tcg_gen_andi_i32(cpu_sr_t, REG(B11_8), 1);
	tcg_gen_shri_i32(REG(B11_8), REG(B11_8), 1);
	return;
    case 0x4008:		/* shll2 Rn */
	tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 2);
	return;
    case 0x4018:		/* shll8 Rn */
	tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 8);
	return;
    case 0x4028:		/* shll16 Rn */
	tcg_gen_shli_i32(REG(B11_8), REG(B11_8), 16);
	return;
    case 0x4009:		/* shlr2 Rn */
	tcg_gen_shri_i32(REG(B11_8), REG(B11_8), 2);
	return;
    case 0x4019:		/* shlr8 Rn */
	tcg_gen_shri_i32(REG(B11_8), REG(B11_8), 8);
	return;
    case 0x4029:		/* shlr16 Rn */
	tcg_gen_shri_i32(REG(B11_8), REG(B11_8), 16);
	return;
    case 0x401b:		/* tas.b @Rn */
        {
            TCGv val = tcg_const_i32(0x80);
            tcg_gen_atomic_fetch_or_i32(val, REG(B11_8), val,
                                        ctx->memidx, MO_UB);
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_sr_t, val, 0);
            tcg_temp_free(val);
        }
        return;
    case 0xf00d: /* fsts FPUL,FRn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        tcg_gen_mov_i32(FREG(B11_8), cpu_fpul);
	return;
    case 0xf01d: /* flds FRm,FPUL - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        tcg_gen_mov_i32(cpu_fpul, FREG(B11_8));
	return;
    case 0xf02d: /* float FPUL,FRn/DRn - FPSCR: R[PR,Enable.I]/W[Cause,Flag] */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_PR) {
	    TCGv_i64 fp;
            if (ctx->opcode & 0x0100) {
                goto do_illegal;
            }
	    fp = tcg_temp_new_i64();
            gen_helper_float_DT(fp, cpu_env, cpu_fpul);
            gen_store_fpr64(ctx, fp, B11_8);
	    tcg_temp_free_i64(fp);
	}
	else {
            gen_helper_float_FT(FREG(B11_8), cpu_env, cpu_fpul);
	}
	return;
    case 0xf03d: /* ftrc FRm/DRm,FPUL - FPSCR: R[PR,Enable.V]/W[Cause,Flag] */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_PR) {
	    TCGv_i64 fp;
            if (ctx->opcode & 0x0100) {
                goto do_illegal;
            }
	    fp = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp, B11_8);
            gen_helper_ftrc_DT(cpu_fpul, cpu_env, fp);
	    tcg_temp_free_i64(fp);
	}
	else {
            gen_helper_ftrc_FT(cpu_fpul, cpu_env, FREG(B11_8));
	}
	return;
    case 0xf04d: /* fneg FRn/DRn - FPSCR: Nothing */
	CHECK_FPU_ENABLED
        tcg_gen_xori_i32(FREG(B11_8), FREG(B11_8), 0x80000000);
	return;
    case 0xf05d: /* fabs FRn/DRn - FPCSR: Nothing */
	CHECK_FPU_ENABLED
        tcg_gen_andi_i32(FREG(B11_8), FREG(B11_8), 0x7fffffff);
	return;
    case 0xf06d: /* fsqrt FRn */
	CHECK_FPU_ENABLED
        if (ctx->tbflags & FPSCR_PR) {
            if (ctx->opcode & 0x0100) {
                goto do_illegal;
            }
	    TCGv_i64 fp = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp, B11_8);
            gen_helper_fsqrt_DT(fp, cpu_env, fp);
            gen_store_fpr64(ctx, fp, B11_8);
	    tcg_temp_free_i64(fp);
	} else {
            gen_helper_fsqrt_FT(FREG(B11_8), cpu_env, FREG(B11_8));
	}
	return;
    case 0xf07d: /* fsrra FRn */
	CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_0
        gen_helper_fsrra_FT(FREG(B11_8), cpu_env, FREG(B11_8));
	break;
    case 0xf08d: /* fldi0 FRn - FPSCR: R[PR] */
	CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_0
        tcg_gen_movi_i32(FREG(B11_8), 0);
        return;
    case 0xf09d: /* fldi1 FRn - FPSCR: R[PR] */
	CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_0
        tcg_gen_movi_i32(FREG(B11_8), 0x3f800000);
        return;
    case 0xf0ad: /* fcnvsd FPUL,DRn */
	CHECK_FPU_ENABLED
	{
	    TCGv_i64 fp = tcg_temp_new_i64();
            gen_helper_fcnvsd_FT_DT(fp, cpu_env, cpu_fpul);
            gen_store_fpr64(ctx, fp, B11_8);
	    tcg_temp_free_i64(fp);
	}
	return;
    case 0xf0bd: /* fcnvds DRn,FPUL */
	CHECK_FPU_ENABLED
	{
	    TCGv_i64 fp = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp, B11_8);
            gen_helper_fcnvds_DT_FT(cpu_fpul, cpu_env, fp);
	    tcg_temp_free_i64(fp);
	}
	return;
    case 0xf0ed: /* fipr FVm,FVn */
        CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_1
        {
            TCGv m = tcg_const_i32((ctx->opcode >> 8) & 3);
            TCGv n = tcg_const_i32((ctx->opcode >> 10) & 3);
            gen_helper_fipr(cpu_env, m, n);
            tcg_temp_free(m);
            tcg_temp_free(n);
            return;
        }
        break;
    case 0xf0fd: /* ftrv XMTRX,FVn */
        CHECK_FPU_ENABLED
        CHECK_FPSCR_PR_1
        {
            if ((ctx->opcode & 0x0300) != 0x0100) {
                goto do_illegal;
            }
            TCGv n = tcg_const_i32((ctx->opcode >> 10) & 3);
            gen_helper_ftrv(cpu_env, n);
            tcg_temp_free(n);
            return;
        }
        break;
    }
#if 0
    fprintf(stderr, "unknown instruction 0x%04x at pc 0x%08x\n",
            ctx->opcode, ctx->base.pc_next);
    fflush(stderr);
#endif
 do_illegal:
    if (ctx->envflags & DELAY_SLOT_MASK) {
 do_illegal_slot:
        gen_save_cpu_state(ctx, true);
        gen_helper_raise_slot_illegal_instruction(cpu_env);
    } else {
        gen_save_cpu_state(ctx, true);
        gen_helper_raise_illegal_instruction(cpu_env);
    }
    ctx->base.is_jmp = DISAS_NORETURN;
    return;

 do_fpu_disabled:
    gen_save_cpu_state(ctx, true);
    if (ctx->envflags & DELAY_SLOT_MASK) {
        gen_helper_raise_slot_fpu_disable(cpu_env);
    } else {
        gen_helper_raise_fpu_disable(cpu_env);
    }
    ctx->base.is_jmp = DISAS_NORETURN;
    return;
}

static void decode_opc(DisasContext * ctx)
{
    uint32_t old_flags = ctx->envflags;

    _decode_opc(ctx);

    if (old_flags & DELAY_SLOT_MASK) {
        /* go out of the delay slot */
        ctx->envflags &= ~DELAY_SLOT_MASK;

        /* When in an exclusive region, we must continue to the end
           for conditional branches.  */
        if (ctx->tbflags & GUSA_EXCLUSIVE
            && old_flags & DELAY_SLOT_CONDITIONAL) {
            gen_delayed_conditional_jump(ctx);
            return;
        }
        /* Otherwise this is probably an invalid gUSA region.
           Drop the GUSA bits so the next TB doesn't see them.  */
        ctx->envflags &= ~GUSA_MASK;

        tcg_gen_movi_i32(cpu_flags, ctx->envflags);
        if (old_flags & DELAY_SLOT_CONDITIONAL) {
	    gen_delayed_conditional_jump(ctx);
        } else {
            gen_jump(ctx);
	}
    }
}

#ifdef CONFIG_USER_ONLY
/* For uniprocessors, SH4 uses optimistic restartable atomic sequences.
   Upon an interrupt, a real kernel would simply notice magic values in
   the registers and reset the PC to the start of the sequence.

   For QEMU, we cannot do this in quite the same way.  Instead, we notice
   the normal start of such a sequence (mov #-x,r15).  While we can handle
   any sequence via cpu_exec_step_atomic, we can recognize the "normal"
   sequences and transform them into atomic operations as seen by the host.
*/
static void decode_gusa(DisasContext *ctx, CPUSH4State *env)
{
    uint16_t insns[5];
    int ld_adr, ld_dst, ld_mop;
    int op_dst, op_src, op_opc;
    int mv_src, mt_dst, st_src, st_mop;
    TCGv op_arg;
    uint32_t pc = ctx->base.pc_next;
    uint32_t pc_end = ctx->base.tb->cs_base;
    int max_insns = (pc_end - pc) / 2;
    int i;

    /* The state machine below will consume only a few insns.
       If there are more than that in a region, fail now.  */
    if (max_insns > ARRAY_SIZE(insns)) {
        goto fail;
    }

    /* Read all of the insns for the region.  */
    for (i = 0; i < max_insns; ++i) {
        insns[i] = translator_lduw(env, pc + i * 2);
    }

    ld_adr = ld_dst = ld_mop = -1;
    mv_src = -1;
    op_dst = op_src = op_opc = -1;
    mt_dst = -1;
    st_src = st_mop = -1;
    op_arg = NULL;
    i = 0;

#define NEXT_INSN \
    do { if (i >= max_insns) goto fail; ctx->opcode = insns[i++]; } while (0)

    /*
     * Expect a load to begin the region.
     */
    NEXT_INSN;
    switch (ctx->opcode & 0xf00f) {
    case 0x6000: /* mov.b @Rm,Rn */
        ld_mop = MO_SB;
        break;
    case 0x6001: /* mov.w @Rm,Rn */
        ld_mop = MO_TESW;
        break;
    case 0x6002: /* mov.l @Rm,Rn */
        ld_mop = MO_TESL;
        break;
    default:
        goto fail;
    }
    ld_adr = B7_4;
    ld_dst = B11_8;
    if (ld_adr == ld_dst) {
        goto fail;
    }
    /* Unless we see a mov, any two-operand operation must use ld_dst.  */
    op_dst = ld_dst;

    /*
     * Expect an optional register move.
     */
    NEXT_INSN;
    switch (ctx->opcode & 0xf00f) {
    case 0x6003: /* mov Rm,Rn */
        /* Here we want to recognize ld_dst being saved for later consumtion,
           or for another input register being copied so that ld_dst need not
           be clobbered during the operation.  */
        op_dst = B11_8;
        mv_src = B7_4;
        if (op_dst == ld_dst) {
            /* Overwriting the load output.  */
            goto fail;
        }
        if (mv_src != ld_dst) {
            /* Copying a new input; constrain op_src to match the load.  */
            op_src = ld_dst;
        }
        break;

    default:
        /* Put back and re-examine as operation.  */
        --i;
    }

    /*
     * Expect the operation.
     */
    NEXT_INSN;
    switch (ctx->opcode & 0xf00f) {
    case 0x300c: /* add Rm,Rn */
        op_opc = INDEX_op_add_i32;
        goto do_reg_op;
    case 0x2009: /* and Rm,Rn */
        op_opc = INDEX_op_and_i32;
        goto do_reg_op;
    case 0x200a: /* xor Rm,Rn */
        op_opc = INDEX_op_xor_i32;
        goto do_reg_op;
    case 0x200b: /* or Rm,Rn */
        op_opc = INDEX_op_or_i32;
    do_reg_op:
        /* The operation register should be as expected, and the
           other input cannot depend on the load.  */
        if (op_dst != B11_8) {
            goto fail;
        }
        if (op_src < 0) {
            /* Unconstrainted input.  */
            op_src = B7_4;
        } else if (op_src == B7_4) {
            /* Constrained input matched load.  All operations are
               commutative; "swap" them by "moving" the load output
               to the (implicit) first argument and the move source
               to the (explicit) second argument.  */
            op_src = mv_src;
        } else {
            goto fail;
        }
        op_arg = REG(op_src);
        break;

    case 0x6007: /* not Rm,Rn */
        if (ld_dst != B7_4 || mv_src >= 0) {
            goto fail;
        }
        op_dst = B11_8;
        op_opc = INDEX_op_xor_i32;
        op_arg = tcg_const_i32(-1);
        break;

    case 0x7000 ... 0x700f: /* add #imm,Rn */
        if (op_dst != B11_8 || mv_src >= 0) {
            goto fail;
        }
        op_opc = INDEX_op_add_i32;
        op_arg = tcg_const_i32(B7_0s);
        break;

    case 0x3000: /* cmp/eq Rm,Rn */
        /* Looking for the middle of a compare-and-swap sequence,
           beginning with the compare.  Operands can be either order,
           but with only one overlapping the load.  */
        if ((ld_dst == B11_8) + (ld_dst == B7_4) != 1 || mv_src >= 0) {
            goto fail;
        }
        op_opc = INDEX_op_setcond_i32;  /* placeholder */
        op_src = (ld_dst == B11_8 ? B7_4 : B11_8);
        op_arg = REG(op_src);

        NEXT_INSN;
        switch (ctx->opcode & 0xff00) {
        case 0x8b00: /* bf label */
        case 0x8f00: /* bf/s label */
            if (pc + (i + 1 + B7_0s) * 2 != pc_end) {
                goto fail;
            }
            if ((ctx->opcode & 0xff00) == 0x8b00) { /* bf label */
                break;
            }
            /* We're looking to unconditionally modify Rn with the
               result of the comparison, within the delay slot of
               the branch.  This is used by older gcc.  */
            NEXT_INSN;
            if ((ctx->opcode & 0xf0ff) == 0x0029) { /* movt Rn */
                mt_dst = B11_8;
            } else {
                goto fail;
            }
            break;

        default:
            goto fail;
        }
        break;

    case 0x2008: /* tst Rm,Rn */
        /* Looking for a compare-and-swap against zero.  */
        if (ld_dst != B11_8 || ld_dst != B7_4 || mv_src >= 0) {
            goto fail;
        }
        op_opc = INDEX_op_setcond_i32;
        op_arg = tcg_const_i32(0);

        NEXT_INSN;
        if ((ctx->opcode & 0xff00) != 0x8900 /* bt label */
            || pc + (i + 1 + B7_0s) * 2 != pc_end) {
            goto fail;
        }
        break;

    default:
        /* Put back and re-examine as store.  */
        --i;
    }

    /*
     * Expect the store.
     */
    /* The store must be the last insn.  */
    if (i != max_insns - 1) {
        goto fail;
    }
    NEXT_INSN;
    switch (ctx->opcode & 0xf00f) {
    case 0x2000: /* mov.b Rm,@Rn */
        st_mop = MO_UB;
        break;
    case 0x2001: /* mov.w Rm,@Rn */
        st_mop = MO_UW;
        break;
    case 0x2002: /* mov.l Rm,@Rn */
        st_mop = MO_UL;
        break;
    default:
        goto fail;
    }
    /* The store must match the load.  */
    if (ld_adr != B11_8 || st_mop != (ld_mop & MO_SIZE)) {
        goto fail;
    }
    st_src = B7_4;

#undef NEXT_INSN

    /*
     * Emit the operation.
     */
    switch (op_opc) {
    case -1:
        /* No operation found.  Look for exchange pattern.  */
        if (st_src == ld_dst || mv_src >= 0) {
            goto fail;
        }
        tcg_gen_atomic_xchg_i32(REG(ld_dst), REG(ld_adr), REG(st_src),
                                ctx->memidx, ld_mop);
        break;

    case INDEX_op_add_i32:
        if (op_dst != st_src) {
            goto fail;
        }
        if (op_dst == ld_dst && st_mop == MO_UL) {
            tcg_gen_atomic_add_fetch_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
        } else {
            tcg_gen_atomic_fetch_add_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
            if (op_dst != ld_dst) {
                /* Note that mop sizes < 4 cannot use add_fetch
                   because it won't carry into the higher bits.  */
                tcg_gen_add_i32(REG(op_dst), REG(ld_dst), op_arg);
            }
        }
        break;

    case INDEX_op_and_i32:
        if (op_dst != st_src) {
            goto fail;
        }
        if (op_dst == ld_dst) {
            tcg_gen_atomic_and_fetch_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
        } else {
            tcg_gen_atomic_fetch_and_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
            tcg_gen_and_i32(REG(op_dst), REG(ld_dst), op_arg);
        }
        break;

    case INDEX_op_or_i32:
        if (op_dst != st_src) {
            goto fail;
        }
        if (op_dst == ld_dst) {
            tcg_gen_atomic_or_fetch_i32(REG(ld_dst), REG(ld_adr),
                                        op_arg, ctx->memidx, ld_mop);
        } else {
            tcg_gen_atomic_fetch_or_i32(REG(ld_dst), REG(ld_adr),
                                        op_arg, ctx->memidx, ld_mop);
            tcg_gen_or_i32(REG(op_dst), REG(ld_dst), op_arg);
        }
        break;

    case INDEX_op_xor_i32:
        if (op_dst != st_src) {
            goto fail;
        }
        if (op_dst == ld_dst) {
            tcg_gen_atomic_xor_fetch_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
        } else {
            tcg_gen_atomic_fetch_xor_i32(REG(ld_dst), REG(ld_adr),
                                         op_arg, ctx->memidx, ld_mop);
            tcg_gen_xor_i32(REG(op_dst), REG(ld_dst), op_arg);
        }
        break;

    case INDEX_op_setcond_i32:
        if (st_src == ld_dst) {
            goto fail;
        }
        tcg_gen_atomic_cmpxchg_i32(REG(ld_dst), REG(ld_adr), op_arg,
                                   REG(st_src), ctx->memidx, ld_mop);
        tcg_gen_setcond_i32(TCG_COND_EQ, cpu_sr_t, REG(ld_dst), op_arg);
        if (mt_dst >= 0) {
            tcg_gen_mov_i32(REG(mt_dst), cpu_sr_t);
        }
        break;

    default:
        g_assert_not_reached();
    }

    /* If op_src is not a valid register, then op_arg was a constant.  */
    if (op_src < 0 && op_arg) {
        tcg_temp_free_i32(op_arg);
    }

    /* The entire region has been translated.  */
    ctx->envflags &= ~GUSA_MASK;
    ctx->base.pc_next = pc_end;
    ctx->base.num_insns += max_insns - 1;
    return;

 fail:
    qemu_log_mask(LOG_UNIMP, "Unrecognized gUSA sequence %08x-%08x\n",
                  pc, pc_end);

    /* Restart with the EXCLUSIVE bit set, within a TB run via
       cpu_exec_step_atomic holding the exclusive lock.  */
    ctx->envflags |= GUSA_EXCLUSIVE;
    gen_save_cpu_state(ctx, false);
    gen_helper_exclusive(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;

    /* We're not executing an instruction, but we must report one for the
       purposes of accounting within the TB.  We might as well report the
       entire region consumed via ctx->base.pc_next so that it's immediately
       available in the disassembly dump.  */
    ctx->base.pc_next = pc_end;
    ctx->base.num_insns += max_insns - 1;
}
#endif

static void sh4_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUSH4State *env = cs->env_ptr;
    uint32_t tbflags;
    int bound;

    ctx->tbflags = tbflags = ctx->base.tb->flags;
    ctx->envflags = tbflags & TB_FLAG_ENVFLAGS_MASK;
    ctx->memidx = (tbflags & (1u << SR_MD)) == 0 ? 1 : 0;
    /* We don't know if the delayed pc came from a dynamic or static branch,
       so assume it is a dynamic branch.  */
    ctx->delayed_pc = -1; /* use delayed pc from env pointer */
    ctx->features = env->features;
    ctx->has_movcal = (tbflags & TB_FLAG_PENDING_MOVCA);
    ctx->gbank = ((tbflags & (1 << SR_MD)) &&
                  (tbflags & (1 << SR_RB))) * 0x10;
    ctx->fbank = tbflags & FPSCR_FR ? 0x10 : 0;

    if (tbflags & GUSA_MASK) {
        uint32_t pc = ctx->base.pc_next;
        uint32_t pc_end = ctx->base.tb->cs_base;
        int backup = sextract32(ctx->tbflags, GUSA_SHIFT, 8);
        int max_insns = (pc_end - pc) / 2;

        if (pc != pc_end + backup || max_insns < 2) {
            /* This is a malformed gUSA region.  Don't do anything special,
               since the interpreter is likely to get confused.  */
            ctx->envflags &= ~GUSA_MASK;
        } else if (tbflags & GUSA_EXCLUSIVE) {
            /* Regardless of single-stepping or the end of the page,
               we must complete execution of the gUSA region while
               holding the exclusive lock.  */
            ctx->base.max_insns = max_insns;
            return;
        }
    }

    /* Since the ISA is fixed-width, we can bound by the number
       of instructions remaining on the page.  */
    bound = -(ctx->base.pc_next | TARGET_PAGE_MASK) / 2;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);
}

static void sh4_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void sh4_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next, ctx->envflags);
}

static bool sh4_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cs,
                                    const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    /* We have hit a breakpoint - make sure PC is up-to-date */
    gen_save_cpu_state(ctx, true);
    gen_helper_debug(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    /* The address covered by the breakpoint must be included in
       [tb->pc, tb->pc + tb->size) in order to for it to be
       properly cleared -- thus we increment the PC here so that
       the logic setting tb->size below does the right thing.  */
    ctx->base.pc_next += 2;
    return true;
}

static void sh4_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPUSH4State *env = cs->env_ptr;
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

#ifdef CONFIG_USER_ONLY
    if (unlikely(ctx->envflags & GUSA_MASK)
        && !(ctx->envflags & GUSA_EXCLUSIVE)) {
        /* We're in an gUSA region, and we have not already fallen
           back on using an exclusive region.  Attempt to parse the
           region into a single supported atomic operation.  Failure
           is handled within the parser by raising an exception to
           retry using an exclusive region.  */
        decode_gusa(ctx, env);
        return;
    }
#endif

    ctx->opcode = translator_lduw(env, ctx->base.pc_next);
    decode_opc(ctx);
    ctx->base.pc_next += 2;
}

static void sh4_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    if (ctx->tbflags & GUSA_EXCLUSIVE) {
        /* Ending the region of exclusivity.  Clear the bits.  */
        ctx->envflags &= ~GUSA_MASK;
    }

    switch (ctx->base.is_jmp) {
    case DISAS_STOP:
        gen_save_cpu_state(ctx, true);
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_save_cpu_state(ctx, false);
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void sh4_tr_disas_log(const DisasContextBase *dcbase, CPUState *cs)
{
    qemu_log("IN:\n");  /* , lookup_symbol(dcbase->pc_first)); */
    log_target_disas(cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps sh4_tr_ops = {
    .init_disas_context = sh4_tr_init_disas_context,
    .tb_start           = sh4_tr_tb_start,
    .insn_start         = sh4_tr_insn_start,
    .breakpoint_check   = sh4_tr_breakpoint_check,
    .translate_insn     = sh4_tr_translate_insn,
    .tb_stop            = sh4_tr_tb_stop,
    .disas_log          = sh4_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&sh4_tr_ops, &ctx.base, cs, tb, max_insns);
}

void restore_state_to_opc(CPUSH4State *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
    env->flags = data[1];
    /* Theoretically delayed_pc should also be restored. In practice the
       branch instruction is re-executed after exception, so the delayed
       branch target will be recomputed. */
}
