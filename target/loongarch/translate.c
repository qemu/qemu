/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation for QEMU - main translation routines.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "exec/translator.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "qemu/qemu-print.h"
#include "fpu/softfloat.h"
#include "translate.h"
#include "internals.h"

/* Global register indices */
TCGv cpu_gpr[32], cpu_pc;
static TCGv cpu_lladdr, cpu_llval;
TCGv_i64 cpu_fpr[32];

#include "exec/gen-icount.h"

#define DISAS_STOP        DISAS_TARGET_0
#define DISAS_EXIT        DISAS_TARGET_1
#define DISAS_EXIT_UPDATE DISAS_TARGET_2

static inline int plus_1(DisasContext *ctx, int x)
{
    return x + 1;
}

static inline int shl_2(DisasContext *ctx, int x)
{
    return x << 2;
}

/*
 * LoongArch the upper 32 bits are undefined ("can be any value").
 * QEMU chooses to nanbox, because it is most likely to show guest bugs early.
 */
static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
}

void generate_exception(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void loongarch_tr_init_disas_context(DisasContextBase *dcbase,
                                            CPUState *cs)
{
    int64_t bound;
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
    ctx->plv = ctx->base.tb->flags & HW_FLAGS_PLV_MASK;
    if (ctx->base.tb->flags & HW_FLAGS_CRMD_PG) {
        ctx->mem_idx = ctx->plv;
    } else {
        ctx->mem_idx = MMU_IDX_DA;
    }

    /* Bound the number of insns to execute to those left on the page.  */
    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);

    ctx->ntemp = 0;
    memset(ctx->temp, 0, sizeof(ctx->temp));

    ctx->zero = tcg_constant_tl(0);
}

static void loongarch_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void loongarch_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

/*
 * Wrappers for getting reg values.
 *
 * The $zero register does not have cpu_gpr[0] allocated -- we supply the
 * constant zero as a source, and an uninitialized sink as destination.
 *
 * Further, we may provide an extension for word operations.
 */
static TCGv temp_new(DisasContext *ctx)
{
    assert(ctx->ntemp < ARRAY_SIZE(ctx->temp));
    return ctx->temp[ctx->ntemp++] = tcg_temp_new();
}

static TCGv gpr_src(DisasContext *ctx, int reg_num, DisasExtend src_ext)
{
    TCGv t;

    if (reg_num == 0) {
        return ctx->zero;
    }

    switch (src_ext) {
    case EXT_NONE:
        return cpu_gpr[reg_num];
    case EXT_SIGN:
        t = temp_new(ctx);
        tcg_gen_ext32s_tl(t, cpu_gpr[reg_num]);
        return t;
    case EXT_ZERO:
        t = temp_new(ctx);
        tcg_gen_ext32u_tl(t, cpu_gpr[reg_num]);
        return t;
    }
    g_assert_not_reached();
}

static TCGv gpr_dst(DisasContext *ctx, int reg_num, DisasExtend dst_ext)
{
    if (reg_num == 0 || dst_ext) {
        return temp_new(ctx);
    }
    return cpu_gpr[reg_num];
}

static void gen_set_gpr(int reg_num, TCGv t, DisasExtend dst_ext)
{
    if (reg_num != 0) {
        switch (dst_ext) {
        case EXT_NONE:
            tcg_gen_mov_tl(cpu_gpr[reg_num], t);
            break;
        case EXT_SIGN:
            tcg_gen_ext32s_tl(cpu_gpr[reg_num], t);
            break;
        case EXT_ZERO:
            tcg_gen_ext32u_tl(cpu_gpr[reg_num], t);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

#include "decode-insns.c.inc"
#include "insn_trans/trans_arith.c.inc"
#include "insn_trans/trans_shift.c.inc"
#include "insn_trans/trans_bit.c.inc"
#include "insn_trans/trans_memory.c.inc"
#include "insn_trans/trans_atomic.c.inc"
#include "insn_trans/trans_extra.c.inc"
#include "insn_trans/trans_farith.c.inc"
#include "insn_trans/trans_fcmp.c.inc"
#include "insn_trans/trans_fcnv.c.inc"
#include "insn_trans/trans_fmov.c.inc"
#include "insn_trans/trans_fmemory.c.inc"
#include "insn_trans/trans_branch.c.inc"
#include "insn_trans/trans_privileged.c.inc"

static void loongarch_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPULoongArchState *env = cs->env_ptr;
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->opcode = cpu_ldl_code(env, ctx->base.pc_next);

    if (!decode(ctx, ctx->opcode)) {
        qemu_log_mask(LOG_UNIMP, "Error: unknown opcode. "
                      TARGET_FMT_lx ": 0x%x\n",
                      ctx->base.pc_next, ctx->opcode);
        generate_exception(ctx, EXCCODE_INE);
    }

    for (int i = ctx->ntemp - 1; i >= 0; --i) {
        tcg_temp_free(ctx->temp[i]);
        ctx->temp[i] = NULL;
    }
    ctx->ntemp = 0;

    ctx->base.pc_next += 4;
}

static void loongarch_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_STOP:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    case DISAS_EXIT_UPDATE:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        QEMU_FALLTHROUGH;
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void loongarch_tr_disas_log(const DisasContextBase *dcbase,
                                   CPUState *cpu, FILE *logfile)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps loongarch_tr_ops = {
    .init_disas_context = loongarch_tr_init_disas_context,
    .tb_start           = loongarch_tr_tb_start,
    .insn_start         = loongarch_tr_insn_start,
    .translate_insn     = loongarch_tr_translate_insn,
    .tb_stop            = loongarch_tr_tb_stop,
    .disas_log          = loongarch_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc,
                    &loongarch_tr_ops, &ctx.base);
}

void loongarch_translate_init(void)
{
    int i;

    cpu_gpr[0] = NULL;
    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
                                        offsetof(CPULoongArchState, gpr[i]),
                                        regnames[i]);
    }

    for (i = 0; i < 32; i++) {
        int off = offsetof(CPULoongArchState, fpr[i]);
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env, off, fregnames[i]);
    }

    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPULoongArchState, pc), "pc");
    cpu_lladdr = tcg_global_mem_new(cpu_env,
                    offsetof(CPULoongArchState, lladdr), "lladdr");
    cpu_llval = tcg_global_mem_new(cpu_env,
                    offsetof(CPULoongArchState, llval), "llval");
}
