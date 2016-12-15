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
    union {
        void (*f_ttt)(TCGv, TCGv, TCGv);
    };
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

/* PA has a habit of taking the LSB of a field and using that as the sign,
   with the rest of the field becoming the least significant bits.  */
static target_long low_sextract(uint32_t val, int pos, int len)
{
    target_ulong x = -(target_ulong)extract32(val, pos, 1);
    x = (x << (len - 1)) | extract32(val, pos + 1, len - 1);
    return x;
}

static target_long assemble_16(uint32_t insn)
{
    /* Take the name from PA2.0, which produces a 16-bit number
       only with wide mode; otherwise a 14-bit number.  Since we don't
       implement wide mode, this is always the 14-bit number.  */
    return low_sextract(insn, 0, 14);
}

static target_long assemble_21(uint32_t insn)
{
    target_ulong x = -(target_ulong)(insn & 1);
    x = (x << 11) | extract32(insn, 1, 11);
    x = (x <<  2) | extract32(insn, 14, 2);
    x = (x <<  5) | extract32(insn, 16, 5);
    x = (x <<  2) | extract32(insn, 12, 2);
    return x << 11;
}

/* The parisc documentation describes only the general interpretation of
   the conditions, without describing their exact implementation.  The
   interpretations do not stand up well when considering ADD,C and SUB,B.
   However, considering the Addition, Subtraction and Logical conditions
   as a whole it would appear that these relations are similar to what
   a traditional NZCV set of flags would produce.  */

static DisasCond do_cond(unsigned cf, TCGv res, TCGv cb_msb, TCGv sv)
{
    DisasCond cond;
    TCGv tmp;

    switch (cf >> 1) {
    case 0: /* Never / TR */
        cond = cond_make_f();
        break;
    case 1: /* = / <>        (Z / !Z) */
        cond = cond_make_0(TCG_COND_EQ, res);
        break;
    case 2: /* < / >=        (N / !N) */
        cond = cond_make_0(TCG_COND_LT, res);
        break;
    case 3: /* <= / >        (N | Z / !N & !Z) */
        cond = cond_make_0(TCG_COND_LE, res);
        break;
    case 4: /* NUV / UV      (!C / C) */
        cond = cond_make_0(TCG_COND_EQ, cb_msb);
        break;
    case 5: /* ZNV / VNZ     (!C | Z / C & !Z) */
        tmp = tcg_temp_new();
        tcg_gen_neg_tl(tmp, cb_msb);
        tcg_gen_and_tl(tmp, tmp, res);
        cond = cond_make_0(TCG_COND_EQ, tmp);
        tcg_temp_free(tmp);
        break;
    case 6: /* SV / NSV      (V / !V) */
        cond = cond_make_0(TCG_COND_LT, sv);
        break;
    case 7: /* OD / EV */
        tmp = tcg_temp_new();
        tcg_gen_andi_tl(tmp, res, 1);
        cond = cond_make_0(TCG_COND_NE, tmp);
        tcg_temp_free(tmp);
        break;
    default:
        g_assert_not_reached();
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/* Similar, but for the special case of subtraction without borrow, we
   can use the inputs directly.  This can allow other computation to be
   deleted as unused.  */

static DisasCond do_sub_cond(unsigned cf, TCGv res, TCGv in1, TCGv in2, TCGv sv)
{
    DisasCond cond;

    switch (cf >> 1) {
    case 1: /* = / <> */
        cond = cond_make(TCG_COND_EQ, in1, in2);
        break;
    case 2: /* < / >= */
        cond = cond_make(TCG_COND_LT, in1, in2);
        break;
    case 3: /* <= / > */
        cond = cond_make(TCG_COND_LE, in1, in2);
        break;
    case 4: /* << / >>= */
        cond = cond_make(TCG_COND_LTU, in1, in2);
        break;
    case 5: /* <<= / >> */
        cond = cond_make(TCG_COND_LEU, in1, in2);
        break;
    default:
        return do_cond(cf, res, sv, sv);
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/* Similar, but for logicals, where the carry and overflow bits are not
   computed, and use of them is undefined.  */

static DisasCond do_log_cond(unsigned cf, TCGv res)
{
    switch (cf >> 1) {
    case 4: case 5: case 6:
        cf &= 1;
        break;
    }
    return do_cond(cf, res, res, res);
}

/* Similar, but for unit conditions.  */

static DisasCond do_unit_cond(unsigned cf, TCGv res, TCGv in1, TCGv in2)
{
    DisasCond cond;
    TCGv tmp, cb;

    TCGV_UNUSED(cb);
    if (cf & 8) {
        /* Since we want to test lots of carry-out bits all at once, do not
         * do our normal thing and compute carry-in of bit B+1 since that
         * leaves us with carry bits spread across two words.
         */
        cb = tcg_temp_new();
        tmp = tcg_temp_new();
        tcg_gen_or_tl(cb, in1, in2);
        tcg_gen_and_tl(tmp, in1, in2);
        tcg_gen_andc_tl(cb, cb, res);
        tcg_gen_or_tl(cb, cb, tmp);
        tcg_temp_free(tmp);
    }

    switch (cf >> 1) {
    case 0: /* never / TR */
    case 1: /* undefined */
    case 5: /* undefined */
        cond = cond_make_f();
        break;

    case 2: /* SBZ / NBZ */
        /* See hasless(v,1) from
         * https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
         */
        tmp = tcg_temp_new();
        tcg_gen_subi_tl(tmp, res, 0x01010101u);
        tcg_gen_andc_tl(tmp, tmp, res);
        tcg_gen_andi_tl(tmp, tmp, 0x80808080u);
        cond = cond_make_0(TCG_COND_NE, tmp);
        tcg_temp_free(tmp);
        break;

    case 3: /* SHZ / NHZ */
        tmp = tcg_temp_new();
        tcg_gen_subi_tl(tmp, res, 0x00010001u);
        tcg_gen_andc_tl(tmp, tmp, res);
        tcg_gen_andi_tl(tmp, tmp, 0x80008000u);
        cond = cond_make_0(TCG_COND_NE, tmp);
        tcg_temp_free(tmp);
        break;

    case 4: /* SDC / NDC */
        tcg_gen_andi_tl(cb, cb, 0x88888888u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    case 6: /* SBC / NBC */
        tcg_gen_andi_tl(cb, cb, 0x80808080u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    case 7: /* SHC / NHC */
        tcg_gen_andi_tl(cb, cb, 0x80008000u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    default:
        g_assert_not_reached();
    }
    if (cf & 8) {
        tcg_temp_free(cb);
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/* Compute signed overflow for addition.  */
static TCGv do_add_sv(DisasContext *ctx, TCGv res, TCGv in1, TCGv in2)
{
    TCGv sv = get_temp(ctx);
    TCGv tmp = tcg_temp_new();

    tcg_gen_xor_tl(sv, res, in1);
    tcg_gen_xor_tl(tmp, in1, in2);
    tcg_gen_andc_tl(sv, sv, tmp);
    tcg_temp_free(tmp);

    return sv;
}

/* Compute signed overflow for subtraction.  */
static TCGv do_sub_sv(DisasContext *ctx, TCGv res, TCGv in1, TCGv in2)
{
    TCGv sv = get_temp(ctx);
    TCGv tmp = tcg_temp_new();

    tcg_gen_xor_tl(sv, res, in1);
    tcg_gen_xor_tl(tmp, in1, in2);
    tcg_gen_and_tl(sv, sv, tmp);
    tcg_temp_free(tmp);

    return sv;
}

static ExitStatus do_add(DisasContext *ctx, unsigned rt, TCGv in1, TCGv in2,
                         unsigned shift, bool is_l, bool is_tsv, bool is_tc,
                         bool is_c, unsigned cf)
{
    TCGv dest, cb, cb_msb, sv, tmp;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new();
    TCGV_UNUSED(cb);
    TCGV_UNUSED(cb_msb);

    if (shift) {
        tmp = get_temp(ctx);
        tcg_gen_shli_tl(tmp, in1, shift);
        in1 = tmp;
    }

    if (!is_l || c == 4 || c == 5) {
        TCGv zero = tcg_const_tl(0);
        cb_msb = get_temp(ctx);
        tcg_gen_add2_tl(dest, cb_msb, in1, zero, in2, zero);
        if (is_c) {
            tcg_gen_add2_tl(dest, cb_msb, dest, cb_msb, cpu_psw_cb_msb, zero);
        }
        tcg_temp_free(zero);
        if (!is_l) {
            cb = get_temp(ctx);
            tcg_gen_xor_tl(cb, in1, in2);
            tcg_gen_xor_tl(cb, cb, dest);
        }
    } else {
        tcg_gen_add_tl(dest, in1, in2);
        if (is_c) {
            tcg_gen_add_tl(dest, dest, cpu_psw_cb_msb);
        }
    }

    /* Compute signed overflow if required.  */
    TCGV_UNUSED(sv);
    if (is_tsv || c == 6) {
        sv = do_add_sv(ctx, dest, in1, in2);
        if (is_tsv) {
            /* ??? Need to include overflow from shift.  */
            gen_helper_tsv(cpu_env, sv);
        }
    }

    /* Emit any conditional trap before any writeback.  */
    cond = do_cond(cf, dest, cb_msb, sv);
    if (is_tc) {
        cond_prep(&cond);
        tmp = tcg_temp_new();
        tcg_gen_setcond_tl(cond.c, tmp, cond.a0, cond.a1);
        gen_helper_tcond(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    /* Write back the result.  */
    if (!is_l) {
        save_or_nullify(ctx, cpu_psw_cb, cb);
        save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    }
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
    return NO_EXIT;
}

static ExitStatus do_sub(DisasContext *ctx, unsigned rt, TCGv in1, TCGv in2,
                         bool is_tsv, bool is_b, bool is_tc, unsigned cf)
{
    TCGv dest, sv, cb, cb_msb, zero, tmp;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new();
    cb = tcg_temp_new();
    cb_msb = tcg_temp_new();

    zero = tcg_const_tl(0);
    if (is_b) {
        /* DEST,C = IN1 + ~IN2 + C.  */
        tcg_gen_not_tl(cb, in2);
        tcg_gen_add2_tl(dest, cb_msb, in1, zero, cpu_psw_cb_msb, zero);
        tcg_gen_add2_tl(dest, cb_msb, dest, cb_msb, cb, zero);
        tcg_gen_xor_tl(cb, cb, in1);
        tcg_gen_xor_tl(cb, cb, dest);
    } else {
        /* DEST,C = IN1 + ~IN2 + 1.  We can produce the same result in fewer
           operations by seeding the high word with 1 and subtracting.  */
        tcg_gen_movi_tl(cb_msb, 1);
        tcg_gen_sub2_tl(dest, cb_msb, in1, cb_msb, in2, zero);
        tcg_gen_eqv_tl(cb, in1, in2);
        tcg_gen_xor_tl(cb, cb, dest);
    }
    tcg_temp_free(zero);

    /* Compute signed overflow if required.  */
    TCGV_UNUSED(sv);
    if (is_tsv || c == 6) {
        sv = do_sub_sv(ctx, dest, in1, in2);
        if (is_tsv) {
            gen_helper_tsv(cpu_env, sv);
        }
    }

    /* Compute the condition.  We cannot use the special case for borrow.  */
    if (!is_b) {
        cond = do_sub_cond(cf, dest, in1, in2, sv);
    } else {
        cond = do_cond(cf, dest, cb_msb, sv);
    }

    /* Emit any conditional trap before any writeback.  */
    if (is_tc) {
        cond_prep(&cond);
        tmp = tcg_temp_new();
        tcg_gen_setcond_tl(cond.c, tmp, cond.a0, cond.a1);
        gen_helper_tcond(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    /* Write back the result.  */
    save_or_nullify(ctx, cpu_psw_cb, cb);
    save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
    return NO_EXIT;
}

static ExitStatus do_cmpclr(DisasContext *ctx, unsigned rt, TCGv in1,
                            TCGv in2, unsigned cf)
{
    TCGv dest, sv;
    DisasCond cond;

    dest = tcg_temp_new();
    tcg_gen_sub_tl(dest, in1, in2);

    /* Compute signed overflow if required.  */
    TCGV_UNUSED(sv);
    if ((cf >> 1) == 6) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    /* Form the condition for the compare.  */
    cond = do_sub_cond(cf, dest, in1, in2, sv);

    /* Clear.  */
    tcg_gen_movi_tl(dest, 0);
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
    return NO_EXIT;
}

static ExitStatus do_log(DisasContext *ctx, unsigned rt, TCGv in1, TCGv in2,
                         unsigned cf, void (*fn)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, rt);

    /* Perform the operation, and writeback.  */
    fn(dest, in1, in2);
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (cf) {
        ctx->null_cond = do_log_cond(cf, dest);
    }
    return NO_EXIT;
}

static ExitStatus do_unit(DisasContext *ctx, unsigned rt, TCGv in1,
                          TCGv in2, unsigned cf, bool is_tc,
                          void (*fn)(TCGv, TCGv, TCGv))
{
    TCGv dest;
    DisasCond cond;

    if (cf == 0) {
        dest = dest_gpr(ctx, rt);
        fn(dest, in1, in2);
        save_gpr(ctx, rt, dest);
        cond_free(&ctx->null_cond);
    } else {
        dest = tcg_temp_new();
        fn(dest, in1, in2);

        cond = do_unit_cond(cf, dest, in1, in2);

        if (is_tc) {
            TCGv tmp = tcg_temp_new();
            cond_prep(&cond);
            tcg_gen_setcond_tl(cond.c, tmp, cond.a0, cond.a1);
            gen_helper_tcond(cpu_env, tmp);
            tcg_temp_free(tmp);
        }
        save_gpr(ctx, rt, dest);

        cond_free(&ctx->null_cond);
        ctx->null_cond = cond;
    }
    return NO_EXIT;
}

static ExitStatus trans_nop(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_add(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned ext = extract32(insn, 8, 4);
    unsigned shift = extract32(insn, 6, 2);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2;
    bool is_c = false;
    bool is_l = false;
    bool is_tc = false;
    bool is_tsv = false;
    ExitStatus ret;

    switch (ext) {
    case 0x6: /* ADD, SHLADD */
        break;
    case 0xa: /* ADD,L, SHLADD,L */
        is_l = true;
        break;
    case 0xe: /* ADD,TSV, SHLADD,TSV (1) */
        is_tsv = true;
        break;
    case 0x7: /* ADD,C */
        is_c = true;
        break;
    case 0xf: /* ADD,C,TSV */
        is_c = is_tsv = true;
        break;
    default:
        return gen_illegal(ctx);
    }

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_add(ctx, rt, tcg_r1, tcg_r2, shift, is_l, is_tsv, is_tc, is_c, cf);
    return nullify_end(ctx, ret);
}

static ExitStatus trans_sub(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned ext = extract32(insn, 6, 6);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2;
    bool is_b = false;
    bool is_tc = false;
    bool is_tsv = false;
    ExitStatus ret;

    switch (ext) {
    case 0x10: /* SUB */
        break;
    case 0x30: /* SUB,TSV */
        is_tsv = true;
        break;
    case 0x14: /* SUB,B */
        is_b = true;
        break;
    case 0x34: /* SUB,B,TSV */
        is_b = is_tsv = true;
        break;
    case 0x13: /* SUB,TC */
        is_tc = true;
        break;
    case 0x33: /* SUB,TSV,TC */
        is_tc = is_tsv = true;
        break;
    default:
        return gen_illegal(ctx);
    }

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_sub(ctx, rt, tcg_r1, tcg_r2, is_tsv, is_b, is_tc, cf);
    return nullify_end(ctx, ret);
}

static ExitStatus trans_log(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_log(ctx, rt, tcg_r1, tcg_r2, cf, di->f_ttt);
    return nullify_end(ctx, ret);
}

/* OR r,0,t -> COPY (according to gas) */
static ExitStatus trans_copy(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned r1 = extract32(insn, 16, 5);
    unsigned rt = extract32(insn,  0, 5);

    if (r1 == 0) {
        TCGv dest = dest_gpr(ctx, rt);
        tcg_gen_movi_tl(dest, 0);
        save_gpr(ctx, rt, dest);
    } else {
        save_gpr(ctx, rt, cpu_gr[r1]);
    }
    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_cmpclr(DisasContext *ctx, uint32_t insn,
                               const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_cmpclr(ctx, rt, tcg_r1, tcg_r2, cf);
    return nullify_end(ctx, ret);
}

static ExitStatus trans_uxor(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_unit(ctx, rt, tcg_r1, tcg_r2, cf, false, tcg_gen_xor_tl);
    return nullify_end(ctx, ret);
}

static ExitStatus trans_uaddcm(DisasContext *ctx, uint32_t insn,
                               const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned is_tc = extract32(insn, 6, 1);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tcg_r1, tcg_r2, tmp;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, r1);
    tcg_r2 = load_gpr(ctx, r2);
    tmp = get_temp(ctx);
    tcg_gen_not_tl(tmp, tcg_r2);
    ret = do_unit(ctx, rt, tcg_r1, tmp, cf, is_tc, tcg_gen_add_tl);
    return nullify_end(ctx, ret);
}

static ExitStatus trans_dcor(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned is_i = extract32(insn, 6, 1);
    unsigned rt = extract32(insn,  0, 5);
    TCGv tmp;
    ExitStatus ret;

    nullify_over(ctx);

    tmp = get_temp(ctx);
    tcg_gen_shri_tl(tmp, cpu_psw_cb, 3);
    if (!is_i) {
        tcg_gen_not_tl(tmp, tmp);
    }
    tcg_gen_andi_tl(tmp, tmp, 0x11111111);
    tcg_gen_muli_tl(tmp, tmp, 6);
    ret = do_unit(ctx, rt, tmp, load_gpr(ctx, r2), cf, false,
                  is_i ? tcg_gen_add_tl : tcg_gen_sub_tl);

    return nullify_end(ctx, ret);
}

static ExitStatus trans_ds(DisasContext *ctx, uint32_t insn,
                           const DisasInsn *di)
{
    unsigned r2 = extract32(insn, 21, 5);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn,  0, 5);
    TCGv dest, add1, add2, addc, zero, in1, in2;

    nullify_over(ctx);

    in1 = load_gpr(ctx, r1);
    in2 = load_gpr(ctx, r2);

    add1 = tcg_temp_new();
    add2 = tcg_temp_new();
    addc = tcg_temp_new();
    dest = tcg_temp_new();
    zero = tcg_const_tl(0);

    /* Form R1 << 1 | PSW[CB]{8}.  */
    tcg_gen_add_tl(add1, in1, in1);
    tcg_gen_add_tl(add1, add1, cpu_psw_cb_msb);

    /* Add or subtract R2, depending on PSW[V].  Proper computation of
       carry{8} requires that we subtract via + ~R2 + 1, as described in
       the manual.  By extracting and masking V, we can produce the
       proper inputs to the addition without movcond.  */
    tcg_gen_sari_tl(addc, cpu_psw_v, TARGET_LONG_BITS - 1);
    tcg_gen_xor_tl(add2, in2, addc);
    tcg_gen_andi_tl(addc, addc, 1);
    /* ??? This is only correct for 32-bit.  */
    tcg_gen_add2_i32(dest, cpu_psw_cb_msb, add1, zero, add2, zero);
    tcg_gen_add2_i32(dest, cpu_psw_cb_msb, dest, cpu_psw_cb_msb, addc, zero);

    tcg_temp_free(addc);
    tcg_temp_free(zero);

    /* Write back the result register.  */
    save_gpr(ctx, rt, dest);

    /* Write back PSW[CB].  */
    tcg_gen_xor_tl(cpu_psw_cb, add1, add2);
    tcg_gen_xor_tl(cpu_psw_cb, cpu_psw_cb, dest);

    /* Write back PSW[V] for the division step.  */
    tcg_gen_neg_tl(cpu_psw_v, cpu_psw_cb_msb);
    tcg_gen_xor_tl(cpu_psw_v, cpu_psw_v, in2);

    /* Install the new nullification.  */
    if (cf) {
        TCGv sv;
        TCGV_UNUSED(sv);
        if (cf >> 1 == 6) {
            /* ??? The lshift is supposed to contribute to overflow.  */
            sv = do_add_sv(ctx, dest, add1, add2);
        }
        ctx->null_cond = do_cond(cf, dest, cpu_psw_cb_msb, sv);
    }

    tcg_temp_free(add1);
    tcg_temp_free(add2);
    tcg_temp_free(dest);

    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_arith_log[] = {
    { 0x08000240u, 0xfc00ffffu, trans_nop },  /* or x,y,0 */
    { 0x08000240u, 0xffe0ffe0u, trans_copy }, /* or x,0,t */
    { 0x08000000u, 0xfc000fe0u, trans_log, .f_ttt = tcg_gen_andc_tl },
    { 0x08000200u, 0xfc000fe0u, trans_log, .f_ttt = tcg_gen_and_tl },
    { 0x08000240u, 0xfc000fe0u, trans_log, .f_ttt = tcg_gen_or_tl },
    { 0x08000280u, 0xfc000fe0u, trans_log, .f_ttt = tcg_gen_xor_tl },
    { 0x08000880u, 0xfc000fe0u, trans_cmpclr },
    { 0x08000380u, 0xfc000fe0u, trans_uxor },
    { 0x08000980u, 0xfc000fa0u, trans_uaddcm },
    { 0x08000b80u, 0xfc1f0fa0u, trans_dcor },
    { 0x08000440u, 0xfc000fe0u, trans_ds },
    { 0x08000700u, 0xfc0007e0u, trans_add }, /* add */
    { 0x08000400u, 0xfc0006e0u, trans_sub }, /* sub; sub,b; sub,tsv */
    { 0x080004c0u, 0xfc0007e0u, trans_sub }, /* sub,tc; sub,tsv,tc */
    { 0x08000200u, 0xfc000320u, trans_add }, /* shladd */
};

static ExitStatus trans_addi(DisasContext *ctx, uint32_t insn)
{
    target_long im = low_sextract(insn, 0, 11);
    unsigned e1 = extract32(insn, 11, 1);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn, 16, 5);
    unsigned r2 = extract32(insn, 21, 5);
    unsigned o1 = extract32(insn, 26, 1);
    TCGv tcg_im, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }

    tcg_im = load_const(ctx, im);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_add(ctx, rt, tcg_im, tcg_r2, 0, false, e1, !o1, false, cf);

    return nullify_end(ctx, ret);
}

static ExitStatus trans_subi(DisasContext *ctx, uint32_t insn)
{
    target_long im = low_sextract(insn, 0, 11);
    unsigned e1 = extract32(insn, 11, 1);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn, 16, 5);
    unsigned r2 = extract32(insn, 21, 5);
    TCGv tcg_im, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }

    tcg_im = load_const(ctx, im);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_sub(ctx, rt, tcg_im, tcg_r2, e1, false, false, cf);

    return nullify_end(ctx, ret);
}

static ExitStatus trans_cmpiclr(DisasContext *ctx, uint32_t insn)
{
    target_long im = low_sextract(insn, 0, 11);
    unsigned cf = extract32(insn, 12, 4);
    unsigned rt = extract32(insn, 16, 5);
    unsigned r2 = extract32(insn, 21, 5);
    TCGv tcg_im, tcg_r2;
    ExitStatus ret;

    if (cf) {
        nullify_over(ctx);
    }

    tcg_im = load_const(ctx, im);
    tcg_r2 = load_gpr(ctx, r2);
    ret = do_cmpclr(ctx, rt, tcg_im, tcg_r2, cf);

    return nullify_end(ctx, ret);
}

static ExitStatus trans_ldil(DisasContext *ctx, uint32_t insn)
{
    unsigned rt = extract32(insn, 21, 5);
    target_long i = assemble_21(insn);
    TCGv tcg_rt = dest_gpr(ctx, rt);

    tcg_gen_movi_tl(tcg_rt, i);
    save_gpr(ctx, rt, tcg_rt);
    cond_free(&ctx->null_cond);

    return NO_EXIT;
}

static ExitStatus trans_addil(DisasContext *ctx, uint32_t insn)
{
    unsigned rt = extract32(insn, 21, 5);
    target_long i = assemble_21(insn);
    TCGv tcg_rt = load_gpr(ctx, rt);
    TCGv tcg_r1 = dest_gpr(ctx, 1);

    tcg_gen_addi_tl(tcg_r1, tcg_rt, i);
    save_gpr(ctx, 1, tcg_r1);
    cond_free(&ctx->null_cond);

    return NO_EXIT;
}

static ExitStatus trans_ldo(DisasContext *ctx, uint32_t insn)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = extract32(insn, 16, 5);
    target_long i = assemble_16(insn);
    TCGv tcg_rt = dest_gpr(ctx, rt);

    /* Special case rb == 0, for the LDI pseudo-op.
       The COPY pseudo-op is handled for free within tcg_gen_addi_tl.  */
    if (rb == 0) {
        tcg_gen_movi_tl(tcg_rt, i);
    } else {
        tcg_gen_addi_tl(tcg_rt, cpu_gr[rb], i);
    }
    save_gpr(ctx, rt, tcg_rt);
    cond_free(&ctx->null_cond);

    return NO_EXIT;
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
    case 0x02:
        return translate_table(ctx, insn, table_arith_log);
    case 0x08:
        return trans_ldil(ctx, insn);
    case 0x0A:
        return trans_addil(ctx, insn);
    case 0x0D:
        return trans_ldo(ctx, insn);
    case 0x24:
        return trans_cmpiclr(ctx, insn);
    case 0x25:
        return trans_subi(ctx, insn);
    case 0x2C:
    case 0x2D:
        return trans_addi(ctx, insn);
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
