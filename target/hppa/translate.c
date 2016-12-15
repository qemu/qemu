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
    if (reg == 0) {
        return get_temp(ctx);
    } else {
        return cpu_gr[reg];
    }
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
    gen_excp_1(exception);
    return EXIT_NORETURN;
}

static ExitStatus gen_illegal(DisasContext *ctx)
{
    return gen_excp(ctx, EXCP_SIGILL);
}

static bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    /* Suppress goto_tb in the case of single-steping and IO.  */
    if ((ctx->tb->cflags & CF_LAST_IO) || ctx->singlestep_enabled) {
        return false;
    }
    return true;
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

            ret = translate_one(&ctx, insn);
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
            ret = EXIT_IAQ_N_STALE;
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
