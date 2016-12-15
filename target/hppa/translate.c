/*
 * HPPA emulation cpu translation for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"

typedef struct DisasCond {
    TCGCond c;
    TCGv a0, a1;
    bool a0_is_n;
    bool a1_is_0;
} DisasCond;

typedef struct DisasContext {
    struct TranslationBlock *tb;
    CPUState *cs;

    target_ulong iaoq_f;
    target_ulong iaoq_b;
    target_ulong iaoq_n;
    TCGv iaoq_n_var;

    int ntemps;
    TCGv temps[8];

    DisasCond null_cond;
    TCGLabel *null_lab;

    bool singlestep_enabled;
    bool psw_n_nonzero;
} DisasContext;

/* Return values from translate_one, indicating the state of the TB.
   Note that zero indicates that we are not exiting the TB.  */

typedef enum {
    NO_EXIT,

    /* We have emitted one or more goto_tb.  No fixup required.  */
    EXIT_GOTO_TB,

    /* We are not using a goto_tb (for whatever reason), but have updated
       the iaq (for whatever reason), so don't do it again on exit.  */
    EXIT_IAQ_N_UPDATED,

    /* We are exiting the TB, but have neither emitted a goto_tb, nor
       updated the iaq for the next instruction to be executed.  */
    EXIT_IAQ_N_STALE,

    /* We are ending the TB with a noreturn function call, e.g. longjmp.
       No following code will be executed.  */
    EXIT_NORETURN,
} ExitStatus;

typedef struct DisasInsn {
    uint32_t insn, mask;
    ExitStatus (*trans)(DisasContext *ctx, uint32_t insn,
                        const struct DisasInsn *f);
} DisasInsn;

/* global register indexes */
static TCGv_env cpu_env;
static TCGv cpu_gr[32];
static TCGv cpu_iaoq_f;
static TCGv cpu_iaoq_b;
static TCGv cpu_sar;
static TCGv cpu_psw_n;
static TCGv cpu_psw_v;
static TCGv cpu_psw_cb;
static TCGv cpu_psw_cb_msb;
static TCGv cpu_cr26;
static TCGv cpu_cr27;

#include "exec/gen-icount.h"

void hppa_translate_init(void)
{
#define DEF_VAR(V)  { &cpu_##V, #V, offsetof(CPUHPPAState, V) }

    typedef struct { TCGv *var; const char *name; int ofs; } GlobalVar;
    static const GlobalVar vars[] = {
        DEF_VAR(sar),
        DEF_VAR(cr26),
        DEF_VAR(cr27),
        DEF_VAR(psw_n),
        DEF_VAR(psw_v),
        DEF_VAR(psw_cb),
        DEF_VAR(psw_cb_msb),
        DEF_VAR(iaoq_f),
        DEF_VAR(iaoq_b),
    };

#undef DEF_VAR

    /* Use the symbolic register names that match the disassembler.  */
    static const char gr_names[32][4] = {
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
    };

    static bool done_init = 0;
    int i;

    if (done_init) {
        return;
    }
    done_init = 1;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    tcg_ctx.tcg_env = cpu_env;

    TCGV_UNUSED(cpu_gr[0]);
    for (i = 1; i < 32; i++) {
        cpu_gr[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUHPPAState, gr[i]),
                                       gr_names[i]);
    }

    for (i = 0; i < ARRAY_SIZE(vars); ++i) {
        const GlobalVar *v = &vars[i];
        *v->var = tcg_global_mem_new(cpu_env, v->ofs, v->name);
    }
}

static DisasCond cond_make_f(void)
{
    DisasCond r = { .c = TCG_COND_NEVER };
    TCGV_UNUSED(r.a0);
    TCGV_UNUSED(r.a1);
    return r;
}

static DisasCond cond_make_n(void)
{
    DisasCond r = { .c = TCG_COND_NE, .a0_is_n = true, .a1_is_0 = true };
    r.a0 = cpu_psw_n;
    TCGV_UNUSED(r.a1);
    return r;
}

static DisasCond cond_make_0(TCGCond c, TCGv a0)
{
    DisasCond r = { .c = c, .a1_is_0 = true };

    assert (c != TCG_COND_NEVER && c != TCG_COND_ALWAYS);
    r.a0 = tcg_temp_new();
    tcg_gen_mov_tl(r.a0, a0);
    TCGV_UNUSED(r.a1);

    return r;
}

static DisasCond cond_make(TCGCond c, TCGv a0, TCGv a1)
{
    DisasCond r = { .c = c };

    assert (c != TCG_COND_NEVER && c != TCG_COND_ALWAYS);
    r.a0 = tcg_temp_new();
    tcg_gen_mov_tl(r.a0, a0);
    r.a1 = tcg_temp_new();
    tcg_gen_mov_tl(r.a1, a1);

    return r;
}

static void cond_prep(DisasCond *cond)
{
    if (cond->a1_is_0) {
        cond->a1_is_0 = false;
        cond->a1 = tcg_const_tl(0);
    }
}

static void cond_free(DisasCond *cond)
{
    switch (cond->c) {
    default:
        if (!cond->a0_is_n) {
            tcg_temp_free(cond->a0);
        }
        if (!cond->a1_is_0) {
            tcg_temp_free(cond->a1);
        }
        cond->a0_is_n = false;
        cond->a1_is_0 = false;
        TCGV_UNUSED(cond->a0);
        TCGV_UNUSED(cond->a1);
        /* fallthru */
    case TCG_COND_ALWAYS:
        cond->c = TCG_COND_NEVER;
        break;
    case TCG_COND_NEVER:
        break;
    }
}

static TCGv get_temp(DisasContext *ctx)
{
    unsigned i = ctx->ntemps++;
    g_assert(i < ARRAY_SIZE(ctx->temps));
    return ctx->temps[i] = tcg_temp_new();
}

static TCGv load_const(DisasContext *ctx, target_long v)
{
    TCGv t = get_temp(ctx);
    tcg_gen_movi_tl(t, v);
    return t;
}

static TCGv load_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0) {
        TCGv t = get_temp(ctx);
        tcg_gen_movi_tl(t, 0);
        return t;
    } else {
        return cpu_gr[reg];
    }
}

static TCGv dest_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0 || ctx->null_cond.c != TCG_COND_NEVER) {
        return get_temp(ctx);
    } else {
        return cpu_gr[reg];
    }
}

static void save_or_nullify(DisasContext *ctx, TCGv dest, TCGv t)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        cond_prep(&ctx->null_cond);
        tcg_gen_movcond_tl(ctx->null_cond.c, dest, ctx->null_cond.a0,
                           ctx->null_cond.a1, dest, t);
    } else {
        tcg_gen_mov_tl(dest, t);
    }
}

static void save_gpr(DisasContext *ctx, unsigned reg, TCGv t)
{
    if (reg != 0) {
        save_or_nullify(ctx, cpu_gr[reg], t);
    }
}

/* Skip over the implementation of an insn that has been nullified.
   Use this when the insn is too complex for a conditional move.  */
static void nullify_over(DisasContext *ctx)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        /* The always condition should have been handled in the main loop.  */
        assert(ctx->null_cond.c != TCG_COND_ALWAYS);

        ctx->null_lab = gen_new_label();
        cond_prep(&ctx->null_cond);

        /* If we're using PSW[N], copy it to a temp because... */
        if (ctx->null_cond.a0_is_n) {
            ctx->null_cond.a0_is_n = false;
            ctx->null_cond.a0 = tcg_temp_new();
            tcg_gen_mov_tl(ctx->null_cond.a0, cpu_psw_n);
        }
        /* ... we clear it before branching over the implementation,
           so that (1) it's clear after nullifying this insn and
           (2) if this insn nullifies the next, PSW[N] is valid.  */
        if (ctx->psw_n_nonzero) {
            ctx->psw_n_nonzero = false;
            tcg_gen_movi_tl(cpu_psw_n, 0);
        }

        tcg_gen_brcond_tl(ctx->null_cond.c, ctx->null_cond.a0,
                          ctx->null_cond.a1, ctx->null_lab);
        cond_free(&ctx->null_cond);
    }
}

/* Save the current nullification state to PSW[N].  */
static void nullify_save(DisasContext *ctx)
{
    if (ctx->null_cond.c == TCG_COND_NEVER) {
        if (ctx->psw_n_nonzero) {
            tcg_gen_movi_tl(cpu_psw_n, 0);
        }
        return;
    }
    if (!ctx->null_cond.a0_is_n) {
        cond_prep(&ctx->null_cond);
        tcg_gen_setcond_tl(ctx->null_cond.c, cpu_psw_n,
                           ctx->null_cond.a0, ctx->null_cond.a1);
        ctx->psw_n_nonzero = true;
    }
    cond_free(&ctx->null_cond);
}

/* Set a PSW[N] to X.  The intention is that this is used immediately
   before a goto_tb/exit_tb, so that there is no fallthru path to other
   code within the TB.  Therefore we do not update psw_n_nonzero.  */
static void nullify_set(DisasContext *ctx, bool x)
{
    if (ctx->psw_n_nonzero || x) {
        tcg_gen_movi_tl(cpu_psw_n, x);
    }
}

/* Mark the end of an instruction that may have been nullified.
   This is the pair to nullify_over.  */
static ExitStatus nullify_end(DisasContext *ctx, ExitStatus status)
{
    TCGLabel *null_lab = ctx->null_lab;

    if (likely(null_lab == NULL)) {
        /* The current insn wasn't conditional or handled the condition
           applied to it without a branch, so the (new) setting of
           NULL_COND can be applied directly to the next insn.  */
        return status;
    }
    ctx->null_lab = NULL;

    if (likely(ctx->null_cond.c == TCG_COND_NEVER)) {
        /* The next instruction will be unconditional,
           and NULL_COND already reflects that.  */
        gen_set_label(null_lab);
    } else {
        /* The insn that we just executed is itself nullifying the next
           instruction.  Store the condition in the PSW[N] global.
           We asserted PSW[N] = 0 in nullify_over, so that after the
           label we have the proper value in place.  */
        nullify_save(ctx);
        gen_set_label(null_lab);
        ctx->null_cond = cond_make_n();
    }

    assert(status != EXIT_GOTO_TB && status != EXIT_IAQ_N_UPDATED);
    if (status == EXIT_NORETURN) {
        status = NO_EXIT;
    }
    return status;
}

static void copy_iaoq_entry(TCGv dest, target_ulong ival, TCGv vval)
{
    if (unlikely(ival == -1)) {
        tcg_gen_mov_tl(dest, vval);
    } else {
        tcg_gen_movi_tl(dest, ival);
    }
}

static inline target_ulong iaoq_dest(DisasContext *ctx, target_long disp)
{
    return ctx->iaoq_f + disp + 8;
}

static void gen_excp_1(int exception)
{
    TCGv_i32 t = tcg_const_i32(exception);
    gen_helper_excp(cpu_env, t);
    tcg_temp_free_i32(t);
}

static ExitStatus gen_excp(DisasContext *ctx, int exception)
{
    copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_f, cpu_iaoq_f);
    copy_iaoq_entry(cpu_iaoq_b, ctx->iaoq_b, cpu_iaoq_b);
    nullify_save(ctx);
    gen_excp_1(exception);
    return EXIT_NORETURN;
}

static ExitStatus gen_illegal(DisasContext *ctx)
{
    nullify_over(ctx);
    return nullify_end(ctx, gen_excp(ctx, EXCP_SIGILL));
}

static bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    /* Suppress goto_tb in the case of single-steping and IO.  */
    if ((ctx->tb->cflags & CF_LAST_IO) || ctx->singlestep_enabled) {
        return false;
    }
    return true;
}

/* If the next insn is to be nullified, and it's on the same page,
   and we're not attempting to set a breakpoint on it, then we can
   totally skip the nullified insn.  This avoids creating and
   executing a TB that merely branches to the next TB.  */
static bool use_nullify_skip(DisasContext *ctx)
{
    return (((ctx->iaoq_b ^ ctx->iaoq_f) & TARGET_PAGE_MASK) == 0
            && !cpu_breakpoint_test(ctx->cs, ctx->iaoq_b, BP_ANY));
}

static void gen_goto_tb(DisasContext *ctx, int which,
                        target_ulong f, target_ulong b)
{
    if (f != -1 && b != -1 && use_goto_tb(ctx, f)) {
        tcg_gen_goto_tb(which);
        tcg_gen_movi_tl(cpu_iaoq_f, f);
        tcg_gen_movi_tl(cpu_iaoq_b, b);
        tcg_gen_exit_tb((uintptr_t)ctx->tb + which);
    } else {
        copy_iaoq_entry(cpu_iaoq_f, f, cpu_iaoq_b);
        copy_iaoq_entry(cpu_iaoq_b, b, ctx->iaoq_n_var);
        if (ctx->singlestep_enabled) {
            gen_excp_1(EXCP_DEBUG);
        } else {
            tcg_gen_exit_tb(0);
        }
    }
}

static ExitStatus translate_table_int(DisasContext *ctx, uint32_t insn,
                                      const DisasInsn table[], size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if ((insn & table[i].mask) == table[i].insn) {
            return table[i].trans(ctx, insn, &table[i]);
        }
    }
    return gen_illegal(ctx);
}

#define translate_table(ctx, insn, table) \
    translate_table_int(ctx, insn, table, ARRAY_SIZE(table))

static ExitStatus translate_one(DisasContext *ctx, uint32_t insn)
{
    uint32_t opc = extract32(insn, 26, 6);

    switch (opc) {
    default:
        break;
    }
    return gen_illegal(ctx);
}

void gen_intermediate_code(CPUHPPAState *env, struct TranslationBlock *tb)
{
    HPPACPU *cpu = hppa_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    DisasContext ctx;
    ExitStatus ret;
    int num_insns, max_insns, i;

    ctx.tb = tb;
    ctx.cs = cs;
    ctx.iaoq_f = tb->pc;
    ctx.iaoq_b = tb->cs_base;
    ctx.singlestep_enabled = cs->singlestep_enabled;

    ctx.ntemps = 0;
    for (i = 0; i < ARRAY_SIZE(ctx.temps); ++i) {
        TCGV_UNUSED(ctx.temps[i]);
    }

    /* Compute the maximum number of insns to execute, as bounded by
       (1) icount, (2) single-stepping, (3) branch delay slots, or
       (4) the number of insns remaining on the current page.  */
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (ctx.singlestep_enabled || singlestep) {
        max_insns = 1;
    } else if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    num_insns = 0;
    gen_tb_start(tb);

    /* Seed the nullification status from PSW[N], as shown in TB->FLAGS.  */
    ctx.null_cond = cond_make_f();
    ctx.psw_n_nonzero = false;
    if (tb->flags & 1) {
        ctx.null_cond.c = TCG_COND_ALWAYS;
        ctx.psw_n_nonzero = true;
    }
    ctx.null_lab = NULL;

    do {
        tcg_gen_insn_start(ctx.iaoq_f, ctx.iaoq_b);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, ctx.iaoq_f, BP_ANY))) {
            ret = gen_excp(&ctx, EXCP_DEBUG);
            break;
        }
        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        {
            /* Always fetch the insn, even if nullified, so that we check
               the page permissions for execute.  */
            uint32_t insn = cpu_ldl_code(env, ctx.iaoq_f);

            /* Set up the IA queue for the next insn.
               This will be overwritten by a branch.  */
            if (ctx.iaoq_b == -1) {
                ctx.iaoq_n = -1;
                ctx.iaoq_n_var = get_temp(&ctx);
                tcg_gen_addi_tl(ctx.iaoq_n_var, cpu_iaoq_b, 4);
            } else {
                ctx.iaoq_n = ctx.iaoq_b + 4;
                TCGV_UNUSED(ctx.iaoq_n_var);
            }

            if (unlikely(ctx.null_cond.c == TCG_COND_ALWAYS)) {
                ctx.null_cond.c = TCG_COND_NEVER;
                ret = NO_EXIT;
            } else {
                ret = translate_one(&ctx, insn);
                assert(ctx.null_lab == NULL);
            }
        }

        for (i = 0; i < ctx.ntemps; ++i) {
            tcg_temp_free(ctx.temps[i]);
            TCGV_UNUSED(ctx.temps[i]);
        }
        ctx.ntemps = 0;

        /* If we see non-linear instructions, exhaust instruction count,
           or run out of buffer space, stop generation.  */
        /* ??? The non-linear instruction restriction is purely due to
           the debugging dump.  Otherwise we *could* follow unconditional
           branches within the same page.  */
        if (ret == NO_EXIT
            && (ctx.iaoq_b != ctx.iaoq_f + 4
                || num_insns >= max_insns
                || tcg_op_buf_full())) {
            if (ctx.null_cond.c == TCG_COND_NEVER
                || ctx.null_cond.c == TCG_COND_ALWAYS) {
                nullify_set(&ctx, ctx.null_cond.c == TCG_COND_ALWAYS);
                gen_goto_tb(&ctx, 0, ctx.iaoq_b, ctx.iaoq_n);
                ret = EXIT_GOTO_TB;
            } else {
                ret = EXIT_IAQ_N_STALE;
            }
        }

        ctx.iaoq_f = ctx.iaoq_b;
        ctx.iaoq_b = ctx.iaoq_n;
        if (ret == EXIT_NORETURN
            || ret == EXIT_GOTO_TB
            || ret == EXIT_IAQ_N_UPDATED) {
            break;
        }
        if (ctx.iaoq_f == -1) {
            tcg_gen_mov_tl(cpu_iaoq_f, cpu_iaoq_b);
            copy_iaoq_entry(cpu_iaoq_b, ctx.iaoq_n, ctx.iaoq_n_var);
            nullify_save(&ctx);
            ret = EXIT_IAQ_N_UPDATED;
            break;
        }
        if (ctx.iaoq_b == -1) {
            tcg_gen_mov_tl(cpu_iaoq_b, ctx.iaoq_n_var);
        }
    } while (ret == NO_EXIT);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    switch (ret) {
    case EXIT_GOTO_TB:
    case EXIT_NORETURN:
        break;
    case EXIT_IAQ_N_STALE:
        copy_iaoq_entry(cpu_iaoq_f, ctx.iaoq_f, cpu_iaoq_f);
        copy_iaoq_entry(cpu_iaoq_b, ctx.iaoq_b, cpu_iaoq_b);
        nullify_save(&ctx);
        /* FALLTHRU */
    case EXIT_IAQ_N_UPDATED:
        if (ctx.singlestep_enabled) {
            gen_excp_1(EXCP_DEBUG);
        } else {
            tcg_gen_exit_tb(0);
        }
        break;
    default:
        abort();
    }

    gen_tb_end(tb, num_insns);

    tb->size = num_insns * 4;
    tb->icount = num_insns;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(tb->pc)) {
        qemu_log_lock();
        qemu_log("IN: %s\n", lookup_symbol(tb->pc));
        log_target_disas(cs, tb->pc, tb->size, 1);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif
}

void restore_state_to_opc(CPUHPPAState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->iaoq_f = data[0];
    if (data[1] != -1) {
        env->iaoq_b = data[1];
    }
    /* Since we were executing the instruction at IAOQ_F, and took some
       sort of action that provoked the cpu_restore_state, we can infer
       that the instruction was not nullified.  */
    env->psw_n = 0;
}
