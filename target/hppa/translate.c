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
        void (*ttt)(TCGv, TCGv, TCGv);
        void (*weww)(TCGv_i32, TCGv_env, TCGv_i32, TCGv_i32);
        void (*dedd)(TCGv_i64, TCGv_env, TCGv_i64, TCGv_i64);
        void (*wew)(TCGv_i32, TCGv_env, TCGv_i32);
        void (*ded)(TCGv_i64, TCGv_env, TCGv_i64);
        void (*wed)(TCGv_i32, TCGv_env, TCGv_i64);
        void (*dew)(TCGv_i64, TCGv_env, TCGv_i32);
    } f;
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

#ifdef HOST_WORDS_BIGENDIAN
# define HI_OFS  0
# define LO_OFS  4
#else
# define HI_OFS  4
# define LO_OFS  0
#endif

static TCGv_i32 load_frw_i32(unsigned rt)
{
    TCGv_i32 ret = tcg_temp_new_i32();
    tcg_gen_ld_i32(ret, cpu_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
    return ret;
}

static TCGv_i32 load_frw0_i32(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i32(0);
    } else {
        return load_frw_i32(rt);
    }
}

static TCGv_i64 load_frw0_i64(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i64(0);
    } else {
        TCGv_i64 ret = tcg_temp_new_i64();
        tcg_gen_ld32u_i64(ret, cpu_env,
                          offsetof(CPUHPPAState, fr[rt & 31])
                          + (rt & 32 ? LO_OFS : HI_OFS));
        return ret;
    }
}

static void save_frw_i32(unsigned rt, TCGv_i32 val)
{
    tcg_gen_st_i32(val, cpu_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
}

#undef HI_OFS
#undef LO_OFS

static TCGv_i64 load_frd(unsigned rt)
{
    TCGv_i64 ret = tcg_temp_new_i64();
    tcg_gen_ld_i64(ret, cpu_env, offsetof(CPUHPPAState, fr[rt]));
    return ret;
}

static TCGv_i64 load_frd0(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i64(0);
    } else {
        return load_frd(rt);
    }
}

static void save_frd(unsigned rt, TCGv_i64 val)
{
    tcg_gen_st_i64(val, cpu_env, offsetof(CPUHPPAState, fr[rt]));
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
            tcg_gen_lookup_and_goto_ptr(cpu_iaoq_f);
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

static unsigned assemble_rt64(uint32_t insn)
{
    unsigned r1 = extract32(insn, 6, 1);
    unsigned r0 = extract32(insn, 0, 5);
    return r1 * 32 + r0;
}

static unsigned assemble_ra64(uint32_t insn)
{
    unsigned r1 = extract32(insn, 7, 1);
    unsigned r0 = extract32(insn, 21, 5);
    return r1 * 32 + r0;
}

static unsigned assemble_rb64(uint32_t insn)
{
    unsigned r1 = extract32(insn, 12, 1);
    unsigned r0 = extract32(insn, 16, 5);
    return r1 * 32 + r0;
}

static unsigned assemble_rc64(uint32_t insn)
{
    unsigned r2 = extract32(insn, 8, 1);
    unsigned r1 = extract32(insn, 13, 3);
    unsigned r0 = extract32(insn, 9, 2);
    return r2 * 32 + r1 * 4 + r0;
}

static target_long assemble_12(uint32_t insn)
{
    target_ulong x = -(target_ulong)(insn & 1);
    x = (x <<  1) | extract32(insn, 2, 1);
    x = (x << 10) | extract32(insn, 3, 10);
    return x;
}

static target_long assemble_16(uint32_t insn)
{
    /* Take the name from PA2.0, which produces a 16-bit number
       only with wide mode; otherwise a 14-bit number.  Since we don't
       implement wide mode, this is always the 14-bit number.  */
    return low_sextract(insn, 0, 14);
}

static target_long assemble_16a(uint32_t insn)
{
    /* Take the name from PA2.0, which produces a 14-bit shifted number
       only with wide mode; otherwise a 12-bit shifted number.  Since we
       don't implement wide mode, this is always the 12-bit number.  */
    target_ulong x = -(target_ulong)(insn & 1);
    x = (x << 11) | extract32(insn, 2, 11);
    return x << 2;
}

static target_long assemble_17(uint32_t insn)
{
    target_ulong x = -(target_ulong)(insn & 1);
    x = (x <<  5) | extract32(insn, 16, 5);
    x = (x <<  1) | extract32(insn, 2, 1);
    x = (x << 10) | extract32(insn, 3, 10);
    return x << 2;
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

static target_long assemble_22(uint32_t insn)
{
    target_ulong x = -(target_ulong)(insn & 1);
    x = (x << 10) | extract32(insn, 16, 10);
    x = (x <<  1) | extract32(insn, 2, 1);
    x = (x << 10) | extract32(insn, 3, 10);
    return x << 2;
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

/* Similar, but for shift/extract/deposit conditions.  */

static DisasCond do_sed_cond(unsigned orig, TCGv res)
{
    unsigned c, f;

    /* Convert the compressed condition codes to standard.
       0-2 are the same as logicals (nv,<,<=), while 3 is OD.
       4-7 are the reverse of 0-3.  */
    c = orig & 3;
    if (c == 3) {
        c = 7;
    }
    f = (orig & 4) / 4;

    return do_log_cond(c * 2 + f, res);
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

/* Emit a memory load.  The modify parameter should be
 * < 0 for pre-modify,
 * > 0 for post-modify,
 * = 0 for no base register update.
 */
static void do_load_32(DisasContext *ctx, TCGv_i32 dest, unsigned rb,
                       unsigned rx, int scale, target_long disp,
                       int modify, TCGMemOp mop)
{
    TCGv addr, base;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    addr = tcg_temp_new();
    base = load_gpr(ctx, rb);

    /* Note that RX is mutually exclusive with DISP.  */
    if (rx) {
        tcg_gen_shli_tl(addr, cpu_gr[rx], scale);
        tcg_gen_add_tl(addr, addr, base);
    } else {
        tcg_gen_addi_tl(addr, base, disp);
    }

    if (modify == 0) {
        tcg_gen_qemu_ld_i32(dest, addr, MMU_USER_IDX, mop);
    } else {
        tcg_gen_qemu_ld_i32(dest, (modify < 0 ? addr : base),
                            MMU_USER_IDX, mop);
        save_gpr(ctx, rb, addr);
    }
    tcg_temp_free(addr);
}

static void do_load_64(DisasContext *ctx, TCGv_i64 dest, unsigned rb,
                       unsigned rx, int scale, target_long disp,
                       int modify, TCGMemOp mop)
{
    TCGv addr, base;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    addr = tcg_temp_new();
    base = load_gpr(ctx, rb);

    /* Note that RX is mutually exclusive with DISP.  */
    if (rx) {
        tcg_gen_shli_tl(addr, cpu_gr[rx], scale);
        tcg_gen_add_tl(addr, addr, base);
    } else {
        tcg_gen_addi_tl(addr, base, disp);
    }

    if (modify == 0) {
        tcg_gen_qemu_ld_i64(dest, addr, MMU_USER_IDX, mop);
    } else {
        tcg_gen_qemu_ld_i64(dest, (modify < 0 ? addr : base),
                            MMU_USER_IDX, mop);
        save_gpr(ctx, rb, addr);
    }
    tcg_temp_free(addr);
}

static void do_store_32(DisasContext *ctx, TCGv_i32 src, unsigned rb,
                        unsigned rx, int scale, target_long disp,
                        int modify, TCGMemOp mop)
{
    TCGv addr, base;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    addr = tcg_temp_new();
    base = load_gpr(ctx, rb);

    /* Note that RX is mutually exclusive with DISP.  */
    if (rx) {
        tcg_gen_shli_tl(addr, cpu_gr[rx], scale);
        tcg_gen_add_tl(addr, addr, base);
    } else {
        tcg_gen_addi_tl(addr, base, disp);
    }

    tcg_gen_qemu_st_i32(src, (modify <= 0 ? addr : base), MMU_USER_IDX, mop);

    if (modify != 0) {
        save_gpr(ctx, rb, addr);
    }
    tcg_temp_free(addr);
}

static void do_store_64(DisasContext *ctx, TCGv_i64 src, unsigned rb,
                        unsigned rx, int scale, target_long disp,
                        int modify, TCGMemOp mop)
{
    TCGv addr, base;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    addr = tcg_temp_new();
    base = load_gpr(ctx, rb);

    /* Note that RX is mutually exclusive with DISP.  */
    if (rx) {
        tcg_gen_shli_tl(addr, cpu_gr[rx], scale);
        tcg_gen_add_tl(addr, addr, base);
    } else {
        tcg_gen_addi_tl(addr, base, disp);
    }

    tcg_gen_qemu_st_i64(src, (modify <= 0 ? addr : base), MMU_USER_IDX, mop);

    if (modify != 0) {
        save_gpr(ctx, rb, addr);
    }
    tcg_temp_free(addr);
}

#if TARGET_LONG_BITS == 64
#define do_load_tl  do_load_64
#define do_store_tl do_store_64
#else
#define do_load_tl  do_load_32
#define do_store_tl do_store_32
#endif

static ExitStatus do_load(DisasContext *ctx, unsigned rt, unsigned rb,
                          unsigned rx, int scale, target_long disp,
                          int modify, TCGMemOp mop)
{
    TCGv dest;

    nullify_over(ctx);

    if (modify == 0) {
        /* No base register update.  */
        dest = dest_gpr(ctx, rt);
    } else {
        /* Make sure if RT == RB, we see the result of the load.  */
        dest = get_temp(ctx);
    }
    do_load_tl(ctx, dest, rb, rx, scale, disp, modify, mop);
    save_gpr(ctx, rt, dest);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_floadw(DisasContext *ctx, unsigned rt, unsigned rb,
                            unsigned rx, int scale, target_long disp,
                            int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i32();
    do_load_32(ctx, tmp, rb, rx, scale, disp, modify, MO_TEUL);
    save_frw_i32(rt, tmp);
    tcg_temp_free_i32(tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(cpu_env);
    }

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_floadd(DisasContext *ctx, unsigned rt, unsigned rb,
                            unsigned rx, int scale, target_long disp,
                            int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    do_load_64(ctx, tmp, rb, rx, scale, disp, modify, MO_TEQ);
    save_frd(rt, tmp);
    tcg_temp_free_i64(tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(cpu_env);
    }

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_store(DisasContext *ctx, unsigned rt, unsigned rb,
                           target_long disp, int modify, TCGMemOp mop)
{
    nullify_over(ctx);
    do_store_tl(ctx, load_gpr(ctx, rt), rb, 0, 0, disp, modify, mop);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fstorew(DisasContext *ctx, unsigned rt, unsigned rb,
                             unsigned rx, int scale, target_long disp,
                             int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = load_frw_i32(rt);
    do_store_32(ctx, tmp, rb, rx, scale, disp, modify, MO_TEUL);
    tcg_temp_free_i32(tmp);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fstored(DisasContext *ctx, unsigned rt, unsigned rb,
                             unsigned rx, int scale, target_long disp,
                             int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = load_frd(rt);
    do_store_64(ctx, tmp, rb, rx, scale, disp, modify, MO_TEQ);
    tcg_temp_free_i64(tmp);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_wew(DisasContext *ctx, unsigned rt, unsigned ra,
                             void (*func)(TCGv_i32, TCGv_env, TCGv_i32))
{
    TCGv_i32 tmp;

    nullify_over(ctx);
    tmp = load_frw0_i32(ra);

    func(tmp, cpu_env, tmp);

    save_frw_i32(rt, tmp);
    tcg_temp_free_i32(tmp);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_wed(DisasContext *ctx, unsigned rt, unsigned ra,
                             void (*func)(TCGv_i32, TCGv_env, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    nullify_over(ctx);
    src = load_frd(ra);
    dst = tcg_temp_new_i32();

    func(dst, cpu_env, src);

    tcg_temp_free_i64(src);
    save_frw_i32(rt, dst);
    tcg_temp_free_i32(dst);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_ded(DisasContext *ctx, unsigned rt, unsigned ra,
                             void (*func)(TCGv_i64, TCGv_env, TCGv_i64))
{
    TCGv_i64 tmp;

    nullify_over(ctx);
    tmp = load_frd0(ra);

    func(tmp, cpu_env, tmp);

    save_frd(rt, tmp);
    tcg_temp_free_i64(tmp);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_dew(DisasContext *ctx, unsigned rt, unsigned ra,
                             void (*func)(TCGv_i64, TCGv_env, TCGv_i32))
{
    TCGv_i32 src;
    TCGv_i64 dst;

    nullify_over(ctx);
    src = load_frw0_i32(ra);
    dst = tcg_temp_new_i64();

    func(dst, cpu_env, src);

    tcg_temp_free_i32(src);
    save_frd(rt, dst);
    tcg_temp_free_i64(dst);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_weww(DisasContext *ctx, unsigned rt,
                              unsigned ra, unsigned rb,
                              void (*func)(TCGv_i32, TCGv_env,
                                           TCGv_i32, TCGv_i32))
{
    TCGv_i32 a, b;

    nullify_over(ctx);
    a = load_frw0_i32(ra);
    b = load_frw0_i32(rb);

    func(a, cpu_env, a, b);

    tcg_temp_free_i32(b);
    save_frw_i32(rt, a);
    tcg_temp_free_i32(a);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus do_fop_dedd(DisasContext *ctx, unsigned rt,
                              unsigned ra, unsigned rb,
                              void (*func)(TCGv_i64, TCGv_env,
                                           TCGv_i64, TCGv_i64))
{
    TCGv_i64 a, b;

    nullify_over(ctx);
    a = load_frd0(ra);
    b = load_frd0(rb);

    func(a, cpu_env, a, b);

    tcg_temp_free_i64(b);
    save_frd(rt, a);
    tcg_temp_free_i64(a);
    return nullify_end(ctx, NO_EXIT);
}

/* Emit an unconditional branch to a direct target, which may or may not
   have already had nullification handled.  */
static ExitStatus do_dbranch(DisasContext *ctx, target_ulong dest,
                             unsigned link, bool is_n)
{
    if (ctx->null_cond.c == TCG_COND_NEVER && ctx->null_lab == NULL) {
        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }
        ctx->iaoq_n = dest;
        if (is_n) {
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
        return NO_EXIT;
    } else {
        nullify_over(ctx);

        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }

        if (is_n && use_nullify_skip(ctx)) {
            nullify_set(ctx, 0);
            gen_goto_tb(ctx, 0, dest, dest + 4);
        } else {
            nullify_set(ctx, is_n);
            gen_goto_tb(ctx, 0, ctx->iaoq_b, dest);
        }

        nullify_end(ctx, NO_EXIT);

        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 1, ctx->iaoq_b, ctx->iaoq_n);
        return EXIT_GOTO_TB;
    }
}

/* Emit a conditional branch to a direct target.  If the branch itself
   is nullified, we should have already used nullify_over.  */
static ExitStatus do_cbranch(DisasContext *ctx, target_long disp, bool is_n,
                             DisasCond *cond)
{
    target_ulong dest = iaoq_dest(ctx, disp);
    TCGLabel *taken = NULL;
    TCGCond c = cond->c;
    bool n;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    /* Handle TRUE and NEVER as direct branches.  */
    if (c == TCG_COND_ALWAYS) {
        return do_dbranch(ctx, dest, 0, is_n && disp >= 0);
    }
    if (c == TCG_COND_NEVER) {
        return do_dbranch(ctx, ctx->iaoq_n, 0, is_n && disp < 0);
    }

    taken = gen_new_label();
    cond_prep(cond);
    tcg_gen_brcond_tl(c, cond->a0, cond->a1, taken);
    cond_free(cond);

    /* Not taken: Condition not satisfied; nullify on backward branches. */
    n = is_n && disp < 0;
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 0, ctx->iaoq_n, ctx->iaoq_n + 4);
    } else {
        if (!n && ctx->null_lab) {
            gen_set_label(ctx->null_lab);
            ctx->null_lab = NULL;
        }
        nullify_set(ctx, n);
        gen_goto_tb(ctx, 0, ctx->iaoq_b, ctx->iaoq_n);
    }

    gen_set_label(taken);

    /* Taken: Condition satisfied; nullify on forward branches.  */
    n = is_n && disp >= 0;
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 1, dest, dest + 4);
    } else {
        nullify_set(ctx, n);
        gen_goto_tb(ctx, 1, ctx->iaoq_b, dest);
    }

    /* Not taken: the branch itself was nullified.  */
    if (ctx->null_lab) {
        gen_set_label(ctx->null_lab);
        ctx->null_lab = NULL;
        return EXIT_IAQ_N_STALE;
    } else {
        return EXIT_GOTO_TB;
    }
}

/* Emit an unconditional branch to an indirect target.  This handles
   nullification of the branch itself.  */
static ExitStatus do_ibranch(DisasContext *ctx, TCGv dest,
                             unsigned link, bool is_n)
{
    TCGv a0, a1, next, tmp;
    TCGCond c;

    assert(ctx->null_lab == NULL);

    if (ctx->null_cond.c == TCG_COND_NEVER) {
        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }
        next = get_temp(ctx);
        tcg_gen_mov_tl(next, dest);
        ctx->iaoq_n = -1;
        ctx->iaoq_n_var = next;
        if (is_n) {
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
    } else if (is_n && use_nullify_skip(ctx)) {
        /* The (conditional) branch, B, nullifies the next insn, N,
           and we're allowed to skip execution N (no single-step or
           tracepoint in effect).  Since the goto_ptr that we must use
           for the indirect branch consumes no special resources, we
           can (conditionally) skip B and continue execution.  */
        /* The use_nullify_skip test implies we have a known control path.  */
        tcg_debug_assert(ctx->iaoq_b != -1);
        tcg_debug_assert(ctx->iaoq_n != -1);

        /* We do have to handle the non-local temporary, DEST, before
           branching.  Since IOAQ_F is not really live at this point, we
           can simply store DEST optimistically.  Similarly with IAOQ_B.  */
        tcg_gen_mov_tl(cpu_iaoq_f, dest);
        tcg_gen_addi_tl(cpu_iaoq_b, dest, 4);

        nullify_over(ctx);
        if (link != 0) {
            tcg_gen_movi_tl(cpu_gr[link], ctx->iaoq_n);
        }
        tcg_gen_lookup_and_goto_ptr(cpu_iaoq_f);
        return nullify_end(ctx, NO_EXIT);
    } else {
        cond_prep(&ctx->null_cond);
        c = ctx->null_cond.c;
        a0 = ctx->null_cond.a0;
        a1 = ctx->null_cond.a1;

        tmp = tcg_temp_new();
        next = get_temp(ctx);

        copy_iaoq_entry(tmp, ctx->iaoq_n, ctx->iaoq_n_var);
        tcg_gen_movcond_tl(c, next, a0, a1, tmp, dest);
        ctx->iaoq_n = -1;
        ctx->iaoq_n_var = next;

        if (link != 0) {
            tcg_gen_movcond_tl(c, cpu_gr[link], a0, a1, cpu_gr[link], tmp);
        }

        if (is_n) {
            /* The branch nullifies the next insn, which means the state of N
               after the branch is the inverse of the state of N that applied
               to the branch.  */
            tcg_gen_setcond_tl(tcg_invert_cond(c), cpu_psw_n, a0, a1);
            cond_free(&ctx->null_cond);
            ctx->null_cond = cond_make_n();
            ctx->psw_n_nonzero = true;
        } else {
            cond_free(&ctx->null_cond);
        }
    }

    return NO_EXIT;
}

/* On Linux, page zero is normally marked execute only + gateway.
   Therefore normal read or write is supposed to fail, but specific
   offsets have kernel code mapped to raise permissions to implement
   system calls.  Handling this via an explicit check here, rather
   in than the "be disp(sr2,r0)" instruction that probably sent us
   here, is the easiest way to handle the branch delay slot on the
   aforementioned BE.  */
static ExitStatus do_page_zero(DisasContext *ctx)
{
    /* If by some means we get here with PSW[N]=1, that implies that
       the B,GATE instruction would be skipped, and we'd fault on the
       next insn within the privilaged page.  */
    switch (ctx->null_cond.c) {
    case TCG_COND_NEVER:
        break;
    case TCG_COND_ALWAYS:
        tcg_gen_movi_tl(cpu_psw_n, 0);
        goto do_sigill;
    default:
        /* Since this is always the first (and only) insn within the
           TB, we should know the state of PSW[N] from TB->FLAGS.  */
        g_assert_not_reached();
    }

    /* Check that we didn't arrive here via some means that allowed
       non-sequential instruction execution.  Normally the PSW[B] bit
       detects this by disallowing the B,GATE instruction to execute
       under such conditions.  */
    if (ctx->iaoq_b != ctx->iaoq_f + 4) {
        goto do_sigill;
    }

    switch (ctx->iaoq_f) {
    case 0x00: /* Null pointer call */
        gen_excp_1(EXCP_SIGSEGV);
        return EXIT_NORETURN;

    case 0xb0: /* LWS */
        gen_excp_1(EXCP_SYSCALL_LWS);
        return EXIT_NORETURN;

    case 0xe0: /* SET_THREAD_POINTER */
        tcg_gen_mov_tl(cpu_cr27, cpu_gr[26]);
        tcg_gen_mov_tl(cpu_iaoq_f, cpu_gr[31]);
        tcg_gen_addi_tl(cpu_iaoq_b, cpu_iaoq_f, 4);
        return EXIT_IAQ_N_UPDATED;

    case 0x100: /* SYSCALL */
        gen_excp_1(EXCP_SYSCALL);
        return EXIT_NORETURN;

    default:
    do_sigill:
        gen_excp_1(EXCP_SIGILL);
        return EXIT_NORETURN;
    }
}

static ExitStatus trans_nop(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_break(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    nullify_over(ctx);
    return nullify_end(ctx, gen_excp(ctx, EXCP_DEBUG));
}

static ExitStatus trans_sync(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    /* No point in nullifying the memory barrier.  */
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_mfia(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    TCGv tmp = dest_gpr(ctx, rt);
    tcg_gen_movi_tl(tmp, ctx->iaoq_f);
    save_gpr(ctx, rt, tmp);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_mfsp(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    TCGv tmp = dest_gpr(ctx, rt);

    /* ??? We don't implement space registers.  */
    tcg_gen_movi_tl(tmp, 0);
    save_gpr(ctx, rt, tmp);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_mfctl(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ctl = extract32(insn, 21, 5);
    TCGv tmp;

    switch (ctl) {
    case 11: /* SAR */
#ifdef TARGET_HPPA64
        if (extract32(insn, 14, 1) == 0) {
            /* MFSAR without ,W masks low 5 bits.  */
            tmp = dest_gpr(ctx, rt);
            tcg_gen_andi_tl(tmp, cpu_sar, 31);
            save_gpr(ctx, rt, tmp);
            break;
        }
#endif
        save_gpr(ctx, rt, cpu_sar);
        break;
    case 16: /* Interval Timer */
        tmp = dest_gpr(ctx, rt);
        tcg_gen_movi_tl(tmp, 0); /* FIXME */
        save_gpr(ctx, rt, tmp);
        break;
    case 26:
        save_gpr(ctx, rt, cpu_cr26);
        break;
    case 27:
        save_gpr(ctx, rt, cpu_cr27);
        break;
    default:
        /* All other control registers are privileged.  */
        return gen_illegal(ctx);
    }

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_mtctl(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    unsigned rin = extract32(insn, 16, 5);
    unsigned ctl = extract32(insn, 21, 5);
    TCGv tmp;

    if (ctl == 11) { /* SAR */
        tmp = tcg_temp_new();
        tcg_gen_andi_tl(tmp, load_gpr(ctx, rin), TARGET_LONG_BITS - 1);
        save_or_nullify(ctx, cpu_sar, tmp);
        tcg_temp_free(tmp);
    } else {
        /* All other control registers are privileged or read-only.  */
        return gen_illegal(ctx);
    }

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_mtsarcm(DisasContext *ctx, uint32_t insn,
                                const DisasInsn *di)
{
    unsigned rin = extract32(insn, 16, 5);
    TCGv tmp = tcg_temp_new();

    tcg_gen_not_tl(tmp, load_gpr(ctx, rin));
    tcg_gen_andi_tl(tmp, tmp, TARGET_LONG_BITS - 1);
    save_or_nullify(ctx, cpu_sar, tmp);
    tcg_temp_free(tmp);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_ldsid(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    TCGv dest = dest_gpr(ctx, rt);

    /* Since we don't implement space registers, this returns zero.  */
    tcg_gen_movi_tl(dest, 0);
    save_gpr(ctx, rt, dest);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static const DisasInsn table_system[] = {
    { 0x00000000u, 0xfc001fe0u, trans_break },
    /* We don't implement space register, so MTSP is a nop.  */
    { 0x00001820u, 0xffe01fffu, trans_nop },
    { 0x00001840u, 0xfc00ffffu, trans_mtctl },
    { 0x016018c0u, 0xffe0ffffu, trans_mtsarcm },
    { 0x000014a0u, 0xffffffe0u, trans_mfia },
    { 0x000004a0u, 0xffff1fe0u, trans_mfsp },
    { 0x000008a0u, 0xfc1fffe0u, trans_mfctl },
    { 0x00000400u, 0xffffffffu, trans_sync },
    { 0x000010a0u, 0xfc1f3fe0u, trans_ldsid },
};

static ExitStatus trans_base_idx_mod(DisasContext *ctx, uint32_t insn,
                                     const DisasInsn *di)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rx = extract32(insn, 16, 5);
    TCGv dest = dest_gpr(ctx, rb);
    TCGv src1 = load_gpr(ctx, rb);
    TCGv src2 = load_gpr(ctx, rx);

    /* The only thing we need to do is the base register modification.  */
    tcg_gen_add_tl(dest, src1, src2);
    save_gpr(ctx, rb, dest);

    cond_free(&ctx->null_cond);
    return NO_EXIT;
}

static ExitStatus trans_probe(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned rb = extract32(insn, 21, 5);
    unsigned is_write = extract32(insn, 6, 1);
    TCGv dest;

    nullify_over(ctx);

    /* ??? Do something with priv level operand.  */
    dest = dest_gpr(ctx, rt);
    if (is_write) {
        gen_helper_probe_w(dest, load_gpr(ctx, rb));
    } else {
        gen_helper_probe_r(dest, load_gpr(ctx, rb));
    }
    save_gpr(ctx, rt, dest);
    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_mem_mgmt[] = {
    { 0x04003280u, 0xfc003fffu, trans_nop },          /* fdc, disp */
    { 0x04001280u, 0xfc003fffu, trans_nop },          /* fdc, index */
    { 0x040012a0u, 0xfc003fffu, trans_base_idx_mod }, /* fdc, index, base mod */
    { 0x040012c0u, 0xfc003fffu, trans_nop },          /* fdce */
    { 0x040012e0u, 0xfc003fffu, trans_base_idx_mod }, /* fdce, base mod */
    { 0x04000280u, 0xfc001fffu, trans_nop },          /* fic 0a */
    { 0x040002a0u, 0xfc001fffu, trans_base_idx_mod }, /* fic 0a, base mod */
    { 0x040013c0u, 0xfc003fffu, trans_nop },          /* fic 4f */
    { 0x040013e0u, 0xfc003fffu, trans_base_idx_mod }, /* fic 4f, base mod */
    { 0x040002c0u, 0xfc001fffu, trans_nop },          /* fice */
    { 0x040002e0u, 0xfc001fffu, trans_base_idx_mod }, /* fice, base mod */
    { 0x04002700u, 0xfc003fffu, trans_nop },          /* pdc */
    { 0x04002720u, 0xfc003fffu, trans_base_idx_mod }, /* pdc, base mod */
    { 0x04001180u, 0xfc003fa0u, trans_probe },        /* probe */
    { 0x04003180u, 0xfc003fa0u, trans_probe },        /* probei */
};

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
    ret = do_log(ctx, rt, tcg_r1, tcg_r2, cf, di->f.ttt);
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
    { 0x08000000u, 0xfc000fe0u, trans_log, .f.ttt = tcg_gen_andc_tl },
    { 0x08000200u, 0xfc000fe0u, trans_log, .f.ttt = tcg_gen_and_tl },
    { 0x08000240u, 0xfc000fe0u, trans_log, .f.ttt = tcg_gen_or_tl },
    { 0x08000280u, 0xfc000fe0u, trans_log, .f.ttt = tcg_gen_xor_tl },
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

static ExitStatus trans_ld_idx_i(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned sz = extract32(insn, 6, 2);
    unsigned a = extract32(insn, 13, 1);
    int disp = low_sextract(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    int modify = (m ? (a ? -1 : 1) : 0);
    TCGMemOp mop = MO_TE | sz;

    return do_load(ctx, rt, rb, 0, 0, disp, modify, mop);
}

static ExitStatus trans_ld_idx_x(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned sz = extract32(insn, 6, 2);
    unsigned u = extract32(insn, 13, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    TCGMemOp mop = MO_TE | sz;

    return do_load(ctx, rt, rb, rx, u ? sz : 0, 0, m, mop);
}

static ExitStatus trans_st_idx_i(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    int disp = low_sextract(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned sz = extract32(insn, 6, 2);
    unsigned a = extract32(insn, 13, 1);
    unsigned rr = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    int modify = (m ? (a ? -1 : 1) : 0);
    TCGMemOp mop = MO_TE | sz;

    return do_store(ctx, rr, rb, disp, modify, mop);
}

static ExitStatus trans_ldcw(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned i = extract32(insn, 12, 1);
    unsigned au = extract32(insn, 13, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    TCGMemOp mop = MO_TEUL | MO_ALIGN_16;
    TCGv zero, addr, base, dest;
    int modify, disp = 0, scale = 0;

    nullify_over(ctx);

    /* ??? Share more code with do_load and do_load_{32,64}.  */

    if (i) {
        modify = (m ? (au ? -1 : 1) : 0);
        disp = low_sextract(rx, 0, 5);
        rx = 0;
    } else {
        modify = m;
        if (au) {
            scale = mop & MO_SIZE;
        }
    }
    if (modify) {
        /* Base register modification.  Make sure if RT == RB, we see
           the result of the load.  */
        dest = get_temp(ctx);
    } else {
        dest = dest_gpr(ctx, rt);
    }

    addr = tcg_temp_new();
    base = load_gpr(ctx, rb);
    if (rx) {
        tcg_gen_shli_tl(addr, cpu_gr[rx], scale);
        tcg_gen_add_tl(addr, addr, base);
    } else {
        tcg_gen_addi_tl(addr, base, disp);
    }

    zero = tcg_const_tl(0);
    tcg_gen_atomic_xchg_tl(dest, (modify <= 0 ? addr : base),
                           zero, MMU_USER_IDX, mop);
    if (modify) {
        save_gpr(ctx, rb, addr);
    }
    save_gpr(ctx, rt, dest);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_stby(DisasContext *ctx, uint32_t insn,
                             const DisasInsn *di)
{
    target_long disp = low_sextract(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned a = extract32(insn, 13, 1);
    unsigned rt = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    TCGv addr, val;

    nullify_over(ctx);

    addr = tcg_temp_new();
    if (m || disp == 0) {
        tcg_gen_mov_tl(addr, load_gpr(ctx, rb));
    } else {
        tcg_gen_addi_tl(addr, load_gpr(ctx, rb), disp);
    }
    val = load_gpr(ctx, rt);

    if (a) {
        gen_helper_stby_e(cpu_env, addr, val);
    } else {
        gen_helper_stby_b(cpu_env, addr, val);
    }

    if (m) {
        tcg_gen_addi_tl(addr, addr, disp);
        tcg_gen_andi_tl(addr, addr, ~3);
        save_gpr(ctx, rb, addr);
    }
    tcg_temp_free(addr);

    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_index_mem[] = {
    { 0x0c001000u, 0xfc001300, trans_ld_idx_i }, /* LD[BHWD], im */
    { 0x0c000000u, 0xfc001300, trans_ld_idx_x }, /* LD[BHWD], rx */
    { 0x0c001200u, 0xfc001300, trans_st_idx_i }, /* ST[BHWD] */
    { 0x0c0001c0u, 0xfc0003c0, trans_ldcw },
    { 0x0c001300u, 0xfc0013c0, trans_stby },
};

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

static ExitStatus trans_load(DisasContext *ctx, uint32_t insn,
                             bool is_mod, TCGMemOp mop)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = extract32(insn, 16, 5);
    target_long i = assemble_16(insn);

    return do_load(ctx, rt, rb, 0, 0, i, is_mod ? (i < 0 ? -1 : 1) : 0, mop);
}

static ExitStatus trans_load_w(DisasContext *ctx, uint32_t insn)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = extract32(insn, 16, 5);
    target_long i = assemble_16a(insn);
    unsigned ext2 = extract32(insn, 1, 2);

    switch (ext2) {
    case 0:
    case 1:
        /* FLDW without modification.  */
        return do_floadw(ctx, ext2 * 32 + rt, rb, 0, 0, i, 0);
    case 2:
        /* LDW with modification.  Note that the sign of I selects
           post-dec vs pre-inc.  */
        return do_load(ctx, rt, rb, 0, 0, i, (i < 0 ? 1 : -1), MO_TEUL);
    default:
        return gen_illegal(ctx);
    }
}

static ExitStatus trans_fload_mod(DisasContext *ctx, uint32_t insn)
{
    target_long i = assemble_16a(insn);
    unsigned t1 = extract32(insn, 1, 1);
    unsigned a = extract32(insn, 2, 1);
    unsigned t0 = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);

    /* FLDW with modification.  */
    return do_floadw(ctx, t1 * 32 + t0, rb, 0, 0, i, (a ? -1 : 1));
}

static ExitStatus trans_store(DisasContext *ctx, uint32_t insn,
                              bool is_mod, TCGMemOp mop)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = extract32(insn, 16, 5);
    target_long i = assemble_16(insn);

    return do_store(ctx, rt, rb, i, is_mod ? (i < 0 ? -1 : 1) : 0, mop);
}

static ExitStatus trans_store_w(DisasContext *ctx, uint32_t insn)
{
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = extract32(insn, 16, 5);
    target_long i = assemble_16a(insn);
    unsigned ext2 = extract32(insn, 1, 2);

    switch (ext2) {
    case 0:
    case 1:
        /* FSTW without modification.  */
        return do_fstorew(ctx, ext2 * 32 + rt, rb, 0, 0, i, 0);
    case 2:
        /* LDW with modification.  */
        return do_store(ctx, rt, rb, i, (i < 0 ? 1 : -1), MO_TEUL);
    default:
        return gen_illegal(ctx);
    }
}

static ExitStatus trans_fstore_mod(DisasContext *ctx, uint32_t insn)
{
    target_long i = assemble_16a(insn);
    unsigned t1 = extract32(insn, 1, 1);
    unsigned a = extract32(insn, 2, 1);
    unsigned t0 = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);

    /* FSTW with modification.  */
    return do_fstorew(ctx, t1 * 32 + t0, rb, 0, 0, i, (a ? -1 : 1));
}

static ExitStatus trans_copr_w(DisasContext *ctx, uint32_t insn)
{
    unsigned t0 = extract32(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned t1 = extract32(insn, 6, 1);
    unsigned ext3 = extract32(insn, 7, 3);
    /* unsigned cc = extract32(insn, 10, 2); */
    unsigned i = extract32(insn, 12, 1);
    unsigned ua = extract32(insn, 13, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    unsigned rt = t1 * 32 + t0;
    int modify = (m ? (ua ? -1 : 1) : 0);
    int disp, scale;

    if (i == 0) {
        scale = (ua ? 2 : 0);
        disp = 0;
        modify = m;
    } else {
        disp = low_sextract(rx, 0, 5);
        scale = 0;
        rx = 0;
        modify = (m ? (ua ? -1 : 1) : 0);
    }

    switch (ext3) {
    case 0: /* FLDW */
        return do_floadw(ctx, rt, rb, rx, scale, disp, modify);
    case 4: /* FSTW */
        return do_fstorew(ctx, rt, rb, rx, scale, disp, modify);
    }
    return gen_illegal(ctx);
}

static ExitStatus trans_copr_dw(DisasContext *ctx, uint32_t insn)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned m = extract32(insn, 5, 1);
    unsigned ext4 = extract32(insn, 6, 4);
    /* unsigned cc = extract32(insn, 10, 2); */
    unsigned i = extract32(insn, 12, 1);
    unsigned ua = extract32(insn, 13, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    int modify = (m ? (ua ? -1 : 1) : 0);
    int disp, scale;

    if (i == 0) {
        scale = (ua ? 3 : 0);
        disp = 0;
        modify = m;
    } else {
        disp = low_sextract(rx, 0, 5);
        scale = 0;
        rx = 0;
        modify = (m ? (ua ? -1 : 1) : 0);
    }

    switch (ext4) {
    case 0: /* FLDD */
        return do_floadd(ctx, rt, rb, rx, scale, disp, modify);
    case 8: /* FSTD */
        return do_fstored(ctx, rt, rb, rx, scale, disp, modify);
    default:
        return gen_illegal(ctx);
    }
}

static ExitStatus trans_cmpb(DisasContext *ctx, uint32_t insn,
                             bool is_true, bool is_imm, bool is_dw)
{
    target_long disp = assemble_12(insn) * 4;
    unsigned n = extract32(insn, 1, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned r = extract32(insn, 21, 5);
    unsigned cf = c * 2 + !is_true;
    TCGv dest, in1, in2, sv;
    DisasCond cond;

    nullify_over(ctx);

    if (is_imm) {
        in1 = load_const(ctx, low_sextract(insn, 16, 5));
    } else {
        in1 = load_gpr(ctx, extract32(insn, 16, 5));
    }
    in2 = load_gpr(ctx, r);
    dest = get_temp(ctx);

    tcg_gen_sub_tl(dest, in1, in2);

    TCGV_UNUSED(sv);
    if (c == 6) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    cond = do_sub_cond(cf, dest, in1, in2, sv);
    return do_cbranch(ctx, disp, n, &cond);
}

static ExitStatus trans_addb(DisasContext *ctx, uint32_t insn,
                             bool is_true, bool is_imm)
{
    target_long disp = assemble_12(insn) * 4;
    unsigned n = extract32(insn, 1, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned r = extract32(insn, 21, 5);
    unsigned cf = c * 2 + !is_true;
    TCGv dest, in1, in2, sv, cb_msb;
    DisasCond cond;

    nullify_over(ctx);

    if (is_imm) {
        in1 = load_const(ctx, low_sextract(insn, 16, 5));
    } else {
        in1 = load_gpr(ctx, extract32(insn, 16, 5));
    }
    in2 = load_gpr(ctx, r);
    dest = dest_gpr(ctx, r);
    TCGV_UNUSED(sv);
    TCGV_UNUSED(cb_msb);

    switch (c) {
    default:
        tcg_gen_add_tl(dest, in1, in2);
        break;
    case 4: case 5:
        cb_msb = get_temp(ctx);
        tcg_gen_movi_tl(cb_msb, 0);
        tcg_gen_add2_tl(dest, cb_msb, in1, cb_msb, in2, cb_msb);
        break;
    case 6:
        tcg_gen_add_tl(dest, in1, in2);
        sv = do_add_sv(ctx, dest, in1, in2);
        break;
    }

    cond = do_cond(cf, dest, cb_msb, sv);
    return do_cbranch(ctx, disp, n, &cond);
}

static ExitStatus trans_bb(DisasContext *ctx, uint32_t insn)
{
    target_long disp = assemble_12(insn) * 4;
    unsigned n = extract32(insn, 1, 1);
    unsigned c = extract32(insn, 15, 1);
    unsigned r = extract32(insn, 16, 5);
    unsigned p = extract32(insn, 21, 5);
    unsigned i = extract32(insn, 26, 1);
    TCGv tmp, tcg_r;
    DisasCond cond;

    nullify_over(ctx);

    tmp = tcg_temp_new();
    tcg_r = load_gpr(ctx, r);
    if (i) {
        tcg_gen_shli_tl(tmp, tcg_r, p);
    } else {
        tcg_gen_shl_tl(tmp, tcg_r, cpu_sar);
    }

    cond = cond_make_0(c ? TCG_COND_GE : TCG_COND_LT, tmp);
    tcg_temp_free(tmp);
    return do_cbranch(ctx, disp, n, &cond);
}

static ExitStatus trans_movb(DisasContext *ctx, uint32_t insn, bool is_imm)
{
    target_long disp = assemble_12(insn) * 4;
    unsigned n = extract32(insn, 1, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned t = extract32(insn, 16, 5);
    unsigned r = extract32(insn, 21, 5);
    TCGv dest;
    DisasCond cond;

    nullify_over(ctx);

    dest = dest_gpr(ctx, r);
    if (is_imm) {
        tcg_gen_movi_tl(dest, low_sextract(t, 0, 5));
    } else if (t == 0) {
        tcg_gen_movi_tl(dest, 0);
    } else {
        tcg_gen_mov_tl(dest, cpu_gr[t]);
    }

    cond = do_sed_cond(c, dest);
    return do_cbranch(ctx, disp, n, &cond);
}

static ExitStatus trans_shrpw_sar(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned c = extract32(insn, 13, 3);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned r2 = extract32(insn, 21, 5);
    TCGv dest;

    if (c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, rt);
    if (r1 == 0) {
        tcg_gen_ext32u_tl(dest, load_gpr(ctx, r2));
        tcg_gen_shr_tl(dest, dest, cpu_sar);
    } else if (r1 == r2) {
        TCGv_i32 t32 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(t32, load_gpr(ctx, r2));
        tcg_gen_rotr_i32(t32, t32, cpu_sar);
        tcg_gen_extu_i32_tl(dest, t32);
        tcg_temp_free_i32(t32);
    } else {
        TCGv_i64 t = tcg_temp_new_i64();
        TCGv_i64 s = tcg_temp_new_i64();

        tcg_gen_concat_tl_i64(t, load_gpr(ctx, r2), load_gpr(ctx, r1));
        tcg_gen_extu_tl_i64(s, cpu_sar);
        tcg_gen_shr_i64(t, t, s);
        tcg_gen_trunc_i64_tl(dest, t);

        tcg_temp_free_i64(t);
        tcg_temp_free_i64(s);
    }
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_shrpw_imm(DisasContext *ctx, uint32_t insn,
                                  const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned cpos = extract32(insn, 5, 5);
    unsigned c = extract32(insn, 13, 3);
    unsigned r1 = extract32(insn, 16, 5);
    unsigned r2 = extract32(insn, 21, 5);
    unsigned sa = 31 - cpos;
    TCGv dest, t2;

    if (c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, rt);
    t2 = load_gpr(ctx, r2);
    if (r1 == r2) {
        TCGv_i32 t32 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(t32, t2);
        tcg_gen_rotri_i32(t32, t32, sa);
        tcg_gen_extu_i32_tl(dest, t32);
        tcg_temp_free_i32(t32);
    } else if (r1 == 0) {
        tcg_gen_extract_tl(dest, t2, sa, 32 - sa);
    } else {
        TCGv t0 = tcg_temp_new();
        tcg_gen_extract_tl(t0, t2, sa, 32 - sa);
        tcg_gen_deposit_tl(dest, t0, cpu_gr[r1], 32 - sa, sa);
        tcg_temp_free(t0);
    }
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_extrw_sar(DisasContext *ctx, uint32_t insn,
                                  const DisasInsn *di)
{
    unsigned clen = extract32(insn, 0, 5);
    unsigned is_se = extract32(insn, 10, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned rt = extract32(insn, 16, 5);
    unsigned rr = extract32(insn, 21, 5);
    unsigned len = 32 - clen;
    TCGv dest, src, tmp;

    if (c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, rt);
    src = load_gpr(ctx, rr);
    tmp = tcg_temp_new();

    /* Recall that SAR is using big-endian bit numbering.  */
    tcg_gen_xori_tl(tmp, cpu_sar, TARGET_LONG_BITS - 1);
    if (is_se) {
        tcg_gen_sar_tl(dest, src, tmp);
        tcg_gen_sextract_tl(dest, dest, 0, len);
    } else {
        tcg_gen_shr_tl(dest, src, tmp);
        tcg_gen_extract_tl(dest, dest, 0, len);
    }
    tcg_temp_free(tmp);
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_extrw_imm(DisasContext *ctx, uint32_t insn,
                                  const DisasInsn *di)
{
    unsigned clen = extract32(insn, 0, 5);
    unsigned pos = extract32(insn, 5, 5);
    unsigned is_se = extract32(insn, 10, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned rt = extract32(insn, 16, 5);
    unsigned rr = extract32(insn, 21, 5);
    unsigned len = 32 - clen;
    unsigned cpos = 31 - pos;
    TCGv dest, src;

    if (c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, rt);
    src = load_gpr(ctx, rr);
    if (is_se) {
        tcg_gen_sextract_tl(dest, src, cpos, len);
    } else {
        tcg_gen_extract_tl(dest, src, cpos, len);
    }
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_sh_ex[] = {
    { 0xd0000000u, 0xfc001fe0u, trans_shrpw_sar },
    { 0xd0000800u, 0xfc001c00u, trans_shrpw_imm },
    { 0xd0001000u, 0xfc001be0u, trans_extrw_sar },
    { 0xd0001800u, 0xfc001800u, trans_extrw_imm },
};

static ExitStatus trans_depw_imm_c(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned clen = extract32(insn, 0, 5);
    unsigned cpos = extract32(insn, 5, 5);
    unsigned nz = extract32(insn, 10, 1);
    unsigned c = extract32(insn, 13, 3);
    target_long val = low_sextract(insn, 16, 5);
    unsigned rt = extract32(insn, 21, 5);
    unsigned len = 32 - clen;
    target_long mask0, mask1;
    TCGv dest;

    if (c) {
        nullify_over(ctx);
    }
    if (cpos + len > 32) {
        len = 32 - cpos;
    }

    dest = dest_gpr(ctx, rt);
    mask0 = deposit64(0, cpos, len, val);
    mask1 = deposit64(-1, cpos, len, val);

    if (nz) {
        TCGv src = load_gpr(ctx, rt);
        if (mask1 != -1) {
            tcg_gen_andi_tl(dest, src, mask1);
            src = dest;
        }
        tcg_gen_ori_tl(dest, src, mask0);
    } else {
        tcg_gen_movi_tl(dest, mask0);
    }
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_depw_imm(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned clen = extract32(insn, 0, 5);
    unsigned cpos = extract32(insn, 5, 5);
    unsigned nz = extract32(insn, 10, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned rr = extract32(insn, 16, 5);
    unsigned rt = extract32(insn, 21, 5);
    unsigned rs = nz ? rt : 0;
    unsigned len = 32 - clen;
    TCGv dest, val;

    if (c) {
        nullify_over(ctx);
    }
    if (cpos + len > 32) {
        len = 32 - cpos;
    }

    dest = dest_gpr(ctx, rt);
    val = load_gpr(ctx, rr);
    if (rs == 0) {
        tcg_gen_deposit_z_tl(dest, val, cpos, len);
    } else {
        tcg_gen_deposit_tl(dest, cpu_gr[rs], val, cpos, len);
    }
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_depw_sar(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned clen = extract32(insn, 0, 5);
    unsigned nz = extract32(insn, 10, 1);
    unsigned i = extract32(insn, 12, 1);
    unsigned c = extract32(insn, 13, 3);
    unsigned rt = extract32(insn, 21, 5);
    unsigned rs = nz ? rt : 0;
    unsigned len = 32 - clen;
    TCGv val, mask, tmp, shift, dest;
    unsigned msb = 1U << (len - 1);

    if (c) {
        nullify_over(ctx);
    }

    if (i) {
        val = load_const(ctx, low_sextract(insn, 16, 5));
    } else {
        val = load_gpr(ctx, extract32(insn, 16, 5));
    }
    dest = dest_gpr(ctx, rt);
    shift = tcg_temp_new();
    tmp = tcg_temp_new();

    /* Convert big-endian bit numbering in SAR to left-shift.  */
    tcg_gen_xori_tl(shift, cpu_sar, TARGET_LONG_BITS - 1);

    mask = tcg_const_tl(msb + (msb - 1));
    tcg_gen_and_tl(tmp, val, mask);
    if (rs) {
        tcg_gen_shl_tl(mask, mask, shift);
        tcg_gen_shl_tl(tmp, tmp, shift);
        tcg_gen_andc_tl(dest, cpu_gr[rs], mask);
        tcg_gen_or_tl(dest, dest, tmp);
    } else {
        tcg_gen_shl_tl(dest, tmp, shift);
    }
    tcg_temp_free(shift);
    tcg_temp_free(mask);
    tcg_temp_free(tmp);
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_depw[] = {
    { 0xd4000000u, 0xfc000be0u, trans_depw_sar },
    { 0xd4000800u, 0xfc001800u, trans_depw_imm },
    { 0xd4001800u, 0xfc001800u, trans_depw_imm_c },
};

static ExitStatus trans_be(DisasContext *ctx, uint32_t insn, bool is_l)
{
    unsigned n = extract32(insn, 1, 1);
    unsigned b = extract32(insn, 21, 5);
    target_long disp = assemble_17(insn);

    /* unsigned s = low_uextract(insn, 13, 3); */
    /* ??? It seems like there should be a good way of using
       "be disp(sr2, r0)", the canonical gateway entry mechanism
       to our advantage.  But that appears to be inconvenient to
       manage along side branch delay slots.  Therefore we handle
       entry into the gateway page via absolute address.  */

    /* Since we don't implement spaces, just branch.  Do notice the special
       case of "be disp(*,r0)" using a direct branch to disp, so that we can
       goto_tb to the TB containing the syscall.  */
    if (b == 0) {
        return do_dbranch(ctx, disp, is_l ? 31 : 0, n);
    } else {
        TCGv tmp = get_temp(ctx);
        tcg_gen_addi_tl(tmp, load_gpr(ctx, b), disp);
        return do_ibranch(ctx, tmp, is_l ? 31 : 0, n);
    }
}

static ExitStatus trans_bl(DisasContext *ctx, uint32_t insn,
                           const DisasInsn *di)
{
    unsigned n = extract32(insn, 1, 1);
    unsigned link = extract32(insn, 21, 5);
    target_long disp = assemble_17(insn);

    return do_dbranch(ctx, iaoq_dest(ctx, disp), link, n);
}

static ExitStatus trans_bl_long(DisasContext *ctx, uint32_t insn,
                                const DisasInsn *di)
{
    unsigned n = extract32(insn, 1, 1);
    target_long disp = assemble_22(insn);

    return do_dbranch(ctx, iaoq_dest(ctx, disp), 2, n);
}

static ExitStatus trans_blr(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    unsigned n = extract32(insn, 1, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned link = extract32(insn, 21, 5);
    TCGv tmp = get_temp(ctx);

    tcg_gen_shli_tl(tmp, load_gpr(ctx, rx), 3);
    tcg_gen_addi_tl(tmp, tmp, ctx->iaoq_f + 8);
    return do_ibranch(ctx, tmp, link, n);
}

static ExitStatus trans_bv(DisasContext *ctx, uint32_t insn,
                           const DisasInsn *di)
{
    unsigned n = extract32(insn, 1, 1);
    unsigned rx = extract32(insn, 16, 5);
    unsigned rb = extract32(insn, 21, 5);
    TCGv dest;

    if (rx == 0) {
        dest = load_gpr(ctx, rb);
    } else {
        dest = get_temp(ctx);
        tcg_gen_shli_tl(dest, load_gpr(ctx, rx), 3);
        tcg_gen_add_tl(dest, dest, load_gpr(ctx, rb));
    }
    return do_ibranch(ctx, dest, 0, n);
}

static ExitStatus trans_bve(DisasContext *ctx, uint32_t insn,
                            const DisasInsn *di)
{
    unsigned n = extract32(insn, 1, 1);
    unsigned rb = extract32(insn, 21, 5);
    unsigned link = extract32(insn, 13, 1) ? 2 : 0;

    return do_ibranch(ctx, load_gpr(ctx, rb), link, n);
}

static const DisasInsn table_branch[] = {
    { 0xe8000000u, 0xfc006000u, trans_bl }, /* B,L and B,L,PUSH */
    { 0xe800a000u, 0xfc00e000u, trans_bl_long },
    { 0xe8004000u, 0xfc00fffdu, trans_blr },
    { 0xe800c000u, 0xfc00fffdu, trans_bv },
    { 0xe800d000u, 0xfc00dffcu, trans_bve },
};

static ExitStatus trans_fop_wew_0c(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_wew(ctx, rt, ra, di->f.wew);
}

static ExitStatus trans_fop_wew_0e(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = assemble_rt64(insn);
    unsigned ra = assemble_ra64(insn);
    return do_fop_wew(ctx, rt, ra, di->f.wew);
}

static ExitStatus trans_fop_ded(DisasContext *ctx, uint32_t insn,
                                const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_ded(ctx, rt, ra, di->f.ded);
}

static ExitStatus trans_fop_wed_0c(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_wed(ctx, rt, ra, di->f.wed);
}

static ExitStatus trans_fop_wed_0e(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = assemble_rt64(insn);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_wed(ctx, rt, ra, di->f.wed);
}

static ExitStatus trans_fop_dew_0c(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_dew(ctx, rt, ra, di->f.dew);
}

static ExitStatus trans_fop_dew_0e(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned ra = assemble_ra64(insn);
    return do_fop_dew(ctx, rt, ra, di->f.dew);
}

static ExitStatus trans_fop_weww_0c(DisasContext *ctx, uint32_t insn,
                                    const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned rb = extract32(insn, 16, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_weww(ctx, rt, ra, rb, di->f.weww);
}

static ExitStatus trans_fop_weww_0e(DisasContext *ctx, uint32_t insn,
                                    const DisasInsn *di)
{
    unsigned rt = assemble_rt64(insn);
    unsigned rb = assemble_rb64(insn);
    unsigned ra = assemble_ra64(insn);
    return do_fop_weww(ctx, rt, ra, rb, di->f.weww);
}

static ExitStatus trans_fop_dedd(DisasContext *ctx, uint32_t insn,
                                 const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned rb = extract32(insn, 16, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fop_dedd(ctx, rt, ra, rb, di->f.dedd);
}

static void gen_fcpy_s(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_mov_i32(dst, src);
}

static void gen_fcpy_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_mov_i64(dst, src);
}

static void gen_fabs_s(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_andi_i32(dst, src, INT32_MAX);
}

static void gen_fabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_andi_i64(dst, src, INT64_MAX);
}

static void gen_fneg_s(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_xori_i32(dst, src, INT32_MIN);
}

static void gen_fneg_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_xori_i64(dst, src, INT64_MIN);
}

static void gen_fnegabs_s(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_ori_i32(dst, src, INT32_MIN);
}

static void gen_fnegabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_ori_i64(dst, src, INT64_MIN);
}

static ExitStatus do_fcmp_s(DisasContext *ctx, unsigned ra, unsigned rb,
                            unsigned y, unsigned c)
{
    TCGv_i32 ta, tb, tc, ty;

    nullify_over(ctx);

    ta = load_frw0_i32(ra);
    tb = load_frw0_i32(rb);
    ty = tcg_const_i32(y);
    tc = tcg_const_i32(c);

    gen_helper_fcmp_s(cpu_env, ta, tb, ty, tc);

    tcg_temp_free_i32(ta);
    tcg_temp_free_i32(tb);
    tcg_temp_free_i32(ty);
    tcg_temp_free_i32(tc);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_fcmp_s_0c(DisasContext *ctx, uint32_t insn,
                                  const DisasInsn *di)
{
    unsigned c = extract32(insn, 0, 5);
    unsigned y = extract32(insn, 13, 3);
    unsigned rb = extract32(insn, 16, 5);
    unsigned ra = extract32(insn, 21, 5);
    return do_fcmp_s(ctx, ra, rb, y, c);
}

static ExitStatus trans_fcmp_s_0e(DisasContext *ctx, uint32_t insn,
                                  const DisasInsn *di)
{
    unsigned c = extract32(insn, 0, 5);
    unsigned y = extract32(insn, 13, 3);
    unsigned rb = assemble_rb64(insn);
    unsigned ra = assemble_ra64(insn);
    return do_fcmp_s(ctx, ra, rb, y, c);
}

static ExitStatus trans_fcmp_d(DisasContext *ctx, uint32_t insn,
                               const DisasInsn *di)
{
    unsigned c = extract32(insn, 0, 5);
    unsigned y = extract32(insn, 13, 3);
    unsigned rb = extract32(insn, 16, 5);
    unsigned ra = extract32(insn, 21, 5);
    TCGv_i64 ta, tb;
    TCGv_i32 tc, ty;

    nullify_over(ctx);

    ta = load_frd0(ra);
    tb = load_frd0(rb);
    ty = tcg_const_i32(y);
    tc = tcg_const_i32(c);

    gen_helper_fcmp_d(cpu_env, ta, tb, ty, tc);

    tcg_temp_free_i64(ta);
    tcg_temp_free_i64(tb);
    tcg_temp_free_i32(ty);
    tcg_temp_free_i32(tc);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_ftest_t(DisasContext *ctx, uint32_t insn,
                                const DisasInsn *di)
{
    unsigned y = extract32(insn, 13, 3);
    unsigned cbit = (y ^ 1) - 1;
    TCGv t;

    nullify_over(ctx);

    t = tcg_temp_new();
    tcg_gen_ld32u_tl(t, cpu_env, offsetof(CPUHPPAState, fr0_shadow));
    tcg_gen_extract_tl(t, t, 21 - cbit, 1);
    ctx->null_cond = cond_make_0(TCG_COND_NE, t);
    tcg_temp_free(t);

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_ftest_q(DisasContext *ctx, uint32_t insn,
                                const DisasInsn *di)
{
    unsigned c = extract32(insn, 0, 5);
    int mask;
    bool inv = false;
    TCGv t;

    nullify_over(ctx);

    t = tcg_temp_new();
    tcg_gen_ld32u_tl(t, cpu_env, offsetof(CPUHPPAState, fr0_shadow));

    switch (c) {
    case 0: /* simple */
        tcg_gen_andi_tl(t, t, 0x4000000);
        ctx->null_cond = cond_make_0(TCG_COND_NE, t);
        goto done;
    case 2: /* rej */
        inv = true;
        /* fallthru */
    case 1: /* acc */
        mask = 0x43ff800;
        break;
    case 6: /* rej8 */
        inv = true;
        /* fallthru */
    case 5: /* acc8 */
        mask = 0x43f8000;
        break;
    case 9: /* acc6 */
        mask = 0x43e0000;
        break;
    case 13: /* acc4 */
        mask = 0x4380000;
        break;
    case 17: /* acc2 */
        mask = 0x4200000;
        break;
    default:
        return gen_illegal(ctx);
    }
    if (inv) {
        TCGv c = load_const(ctx, mask);
        tcg_gen_or_tl(t, t, c);
        ctx->null_cond = cond_make(TCG_COND_EQ, t, c);
    } else {
        tcg_gen_andi_tl(t, t, mask);
        ctx->null_cond = cond_make_0(TCG_COND_EQ, t);
    }
 done:
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_xmpyu(DisasContext *ctx, uint32_t insn,
                              const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned rb = assemble_rb64(insn);
    unsigned ra = assemble_ra64(insn);
    TCGv_i64 a, b;

    nullify_over(ctx);

    a = load_frw0_i64(ra);
    b = load_frw0_i64(rb);
    tcg_gen_mul_i64(a, a, b);
    save_frd(rt, a);
    tcg_temp_free_i64(a);
    tcg_temp_free_i64(b);

    return nullify_end(ctx, NO_EXIT);
}

#define FOP_DED  trans_fop_ded, .f.ded
#define FOP_DEDD trans_fop_dedd, .f.dedd

#define FOP_WEW  trans_fop_wew_0c, .f.wew
#define FOP_DEW  trans_fop_dew_0c, .f.dew
#define FOP_WED  trans_fop_wed_0c, .f.wed
#define FOP_WEWW trans_fop_weww_0c, .f.weww

static const DisasInsn table_float_0c[] = {
    /* floating point class zero */
    { 0x30004000, 0xfc1fffe0, FOP_WEW = gen_fcpy_s },
    { 0x30006000, 0xfc1fffe0, FOP_WEW = gen_fabs_s },
    { 0x30008000, 0xfc1fffe0, FOP_WEW = gen_helper_fsqrt_s },
    { 0x3000a000, 0xfc1fffe0, FOP_WEW = gen_helper_frnd_s },
    { 0x3000c000, 0xfc1fffe0, FOP_WEW = gen_fneg_s },
    { 0x3000e000, 0xfc1fffe0, FOP_WEW = gen_fnegabs_s },

    { 0x30004800, 0xfc1fffe0, FOP_DED = gen_fcpy_d },
    { 0x30006800, 0xfc1fffe0, FOP_DED = gen_fabs_d },
    { 0x30008800, 0xfc1fffe0, FOP_DED = gen_helper_fsqrt_d },
    { 0x3000a800, 0xfc1fffe0, FOP_DED = gen_helper_frnd_d },
    { 0x3000c800, 0xfc1fffe0, FOP_DED = gen_fneg_d },
    { 0x3000e800, 0xfc1fffe0, FOP_DED = gen_fnegabs_d },

    /* floating point class three */
    { 0x30000600, 0xfc00ffe0, FOP_WEWW = gen_helper_fadd_s },
    { 0x30002600, 0xfc00ffe0, FOP_WEWW = gen_helper_fsub_s },
    { 0x30004600, 0xfc00ffe0, FOP_WEWW = gen_helper_fmpy_s },
    { 0x30006600, 0xfc00ffe0, FOP_WEWW = gen_helper_fdiv_s },

    { 0x30000e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fadd_d },
    { 0x30002e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fsub_d },
    { 0x30004e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fmpy_d },
    { 0x30006e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fdiv_d },

    /* floating point class one */
    /* float/float */
    { 0x30000a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_d_s },
    { 0x30002200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_s_d },
    /* int/float */
    { 0x30008200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_w_s },
    { 0x30008a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_dw_s },
    { 0x3000a200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_w_d },
    { 0x3000aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_dw_d },
    /* float/int */
    { 0x30010200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_s_w },
    { 0x30010a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_d_w },
    { 0x30012200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_s_dw },
    { 0x30012a00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_d_dw },
    /* float/int truncate */
    { 0x30018200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_t_s_w },
    { 0x30018a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_t_d_w },
    { 0x3001a200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_t_s_dw },
    { 0x3001aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_t_d_dw },
    /* uint/float */
    { 0x30028200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_uw_s },
    { 0x30028a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_udw_s },
    { 0x3002a200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_uw_d },
    { 0x3002aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_udw_d },
    /* float/uint */
    { 0x30030200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_s_uw },
    { 0x30030a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_d_uw },
    { 0x30032200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_s_udw },
    { 0x30032a00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_d_udw },
    /* float/uint truncate */
    { 0x30038200, 0xfc1fffe0, FOP_WEW = gen_helper_fcnv_t_s_uw },
    { 0x30038a00, 0xfc1fffe0, FOP_WED = gen_helper_fcnv_t_d_uw },
    { 0x3003a200, 0xfc1fffe0, FOP_DEW = gen_helper_fcnv_t_s_udw },
    { 0x3003aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_t_d_udw },

    /* floating point class two */
    { 0x30000400, 0xfc001fe0, trans_fcmp_s_0c },
    { 0x30000c00, 0xfc001fe0, trans_fcmp_d },
    { 0x30002420, 0xffffffe0, trans_ftest_q },
    { 0x30000420, 0xffff1fff, trans_ftest_t },

    /* FID.  Note that ra == rt == 0, which via fcpy puts 0 into fr0.
       This is machine/revision == 0, which is reserved for simulator.  */
    { 0x30000000, 0xffffffff, FOP_WEW = gen_fcpy_s },
};

#undef FOP_WEW
#undef FOP_DEW
#undef FOP_WED
#undef FOP_WEWW
#define FOP_WEW  trans_fop_wew_0e, .f.wew
#define FOP_DEW  trans_fop_dew_0e, .f.dew
#define FOP_WED  trans_fop_wed_0e, .f.wed
#define FOP_WEWW trans_fop_weww_0e, .f.weww

static const DisasInsn table_float_0e[] = {
    /* floating point class zero */
    { 0x38004000, 0xfc1fff20, FOP_WEW = gen_fcpy_s },
    { 0x38006000, 0xfc1fff20, FOP_WEW = gen_fabs_s },
    { 0x38008000, 0xfc1fff20, FOP_WEW = gen_helper_fsqrt_s },
    { 0x3800a000, 0xfc1fff20, FOP_WEW = gen_helper_frnd_s },
    { 0x3800c000, 0xfc1fff20, FOP_WEW = gen_fneg_s },
    { 0x3800e000, 0xfc1fff20, FOP_WEW = gen_fnegabs_s },

    { 0x38004800, 0xfc1fffe0, FOP_DED = gen_fcpy_d },
    { 0x38006800, 0xfc1fffe0, FOP_DED = gen_fabs_d },
    { 0x38008800, 0xfc1fffe0, FOP_DED = gen_helper_fsqrt_d },
    { 0x3800a800, 0xfc1fffe0, FOP_DED = gen_helper_frnd_d },
    { 0x3800c800, 0xfc1fffe0, FOP_DED = gen_fneg_d },
    { 0x3800e800, 0xfc1fffe0, FOP_DED = gen_fnegabs_d },

    /* floating point class three */
    { 0x38000600, 0xfc00ef20, FOP_WEWW = gen_helper_fadd_s },
    { 0x38002600, 0xfc00ef20, FOP_WEWW = gen_helper_fsub_s },
    { 0x38004600, 0xfc00ef20, FOP_WEWW = gen_helper_fmpy_s },
    { 0x38006600, 0xfc00ef20, FOP_WEWW = gen_helper_fdiv_s },

    { 0x38000e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fadd_d },
    { 0x38002e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fsub_d },
    { 0x38004e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fmpy_d },
    { 0x38006e00, 0xfc00ffe0, FOP_DEDD = gen_helper_fdiv_d },

    { 0x38004700, 0xfc00ef60, trans_xmpyu },

    /* floating point class one */
    /* float/float */
    { 0x38000a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_d_s },
    { 0x38002200, 0xfc1fffc0, FOP_DEW = gen_helper_fcnv_s_d },
    /* int/float */
    { 0x38008200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_w_s },
    { 0x38008a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_dw_s },
    { 0x3800a200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_w_d },
    { 0x3800aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_dw_d },
    /* float/int */
    { 0x38010200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_s_w },
    { 0x38010a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_d_w },
    { 0x38012200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_s_dw },
    { 0x38012a00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_d_dw },
    /* float/int truncate */
    { 0x38018200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_t_s_w },
    { 0x38018a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_t_d_w },
    { 0x3801a200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_t_s_dw },
    { 0x3801aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_t_d_dw },
    /* uint/float */
    { 0x38028200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_uw_s },
    { 0x38028a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_udw_s },
    { 0x3802a200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_uw_d },
    { 0x3802aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_udw_d },
    /* float/uint */
    { 0x38030200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_s_uw },
    { 0x38030a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_d_uw },
    { 0x38032200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_s_udw },
    { 0x38032a00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_d_udw },
    /* float/uint truncate */
    { 0x38038200, 0xfc1ffe60, FOP_WEW = gen_helper_fcnv_t_s_uw },
    { 0x38038a00, 0xfc1fffa0, FOP_WED = gen_helper_fcnv_t_d_uw },
    { 0x3803a200, 0xfc1fff60, FOP_DEW = gen_helper_fcnv_t_s_udw },
    { 0x3803aa00, 0xfc1fffe0, FOP_DED = gen_helper_fcnv_t_d_udw },

    /* floating point class two */
    { 0x38000400, 0xfc000f60, trans_fcmp_s_0e },
    { 0x38000c00, 0xfc001fe0, trans_fcmp_d },
};

#undef FOP_WEW
#undef FOP_DEW
#undef FOP_WED
#undef FOP_WEWW
#undef FOP_DED
#undef FOP_DEDD

/* Convert the fmpyadd single-precision register encodings to standard.  */
static inline int fmpyadd_s_reg(unsigned r)
{
    return (r & 16) * 2 + 16 + (r & 15);
}

static ExitStatus trans_fmpyadd(DisasContext *ctx, uint32_t insn, bool is_sub)
{
    unsigned tm = extract32(insn, 0, 5);
    unsigned f = extract32(insn, 5, 1);
    unsigned ra = extract32(insn, 6, 5);
    unsigned ta = extract32(insn, 11, 5);
    unsigned rm2 = extract32(insn, 16, 5);
    unsigned rm1 = extract32(insn, 21, 5);

    nullify_over(ctx);

    /* Independent multiply & add/sub, with undefined behaviour
       if outputs overlap inputs.  */
    if (f == 0) {
        tm = fmpyadd_s_reg(tm);
        ra = fmpyadd_s_reg(ra);
        ta = fmpyadd_s_reg(ta);
        rm2 = fmpyadd_s_reg(rm2);
        rm1 = fmpyadd_s_reg(rm1);
        do_fop_weww(ctx, tm, rm1, rm2, gen_helper_fmpy_s);
        do_fop_weww(ctx, ta, ta, ra,
                    is_sub ? gen_helper_fsub_s : gen_helper_fadd_s);
    } else {
        do_fop_dedd(ctx, tm, rm1, rm2, gen_helper_fmpy_d);
        do_fop_dedd(ctx, ta, ta, ra,
                    is_sub ? gen_helper_fsub_d : gen_helper_fadd_d);
    }

    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_fmpyfadd_s(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = assemble_rt64(insn);
    unsigned neg = extract32(insn, 5, 1);
    unsigned rm1 = assemble_ra64(insn);
    unsigned rm2 = assemble_rb64(insn);
    unsigned ra3 = assemble_rc64(insn);
    TCGv_i32 a, b, c;

    nullify_over(ctx);
    a = load_frw0_i32(rm1);
    b = load_frw0_i32(rm2);
    c = load_frw0_i32(ra3);

    if (neg) {
        gen_helper_fmpynfadd_s(a, cpu_env, a, b, c);
    } else {
        gen_helper_fmpyfadd_s(a, cpu_env, a, b, c);
    }

    tcg_temp_free_i32(b);
    tcg_temp_free_i32(c);
    save_frw_i32(rt, a);
    tcg_temp_free_i32(a);
    return nullify_end(ctx, NO_EXIT);
}

static ExitStatus trans_fmpyfadd_d(DisasContext *ctx, uint32_t insn,
                                   const DisasInsn *di)
{
    unsigned rt = extract32(insn, 0, 5);
    unsigned neg = extract32(insn, 5, 1);
    unsigned rm1 = extract32(insn, 21, 5);
    unsigned rm2 = extract32(insn, 16, 5);
    unsigned ra3 = assemble_rc64(insn);
    TCGv_i64 a, b, c;

    nullify_over(ctx);
    a = load_frd0(rm1);
    b = load_frd0(rm2);
    c = load_frd0(ra3);

    if (neg) {
        gen_helper_fmpynfadd_d(a, cpu_env, a, b, c);
    } else {
        gen_helper_fmpyfadd_d(a, cpu_env, a, b, c);
    }

    tcg_temp_free_i64(b);
    tcg_temp_free_i64(c);
    save_frd(rt, a);
    tcg_temp_free_i64(a);
    return nullify_end(ctx, NO_EXIT);
}

static const DisasInsn table_fp_fused[] = {
    { 0xb8000000u, 0xfc000800u, trans_fmpyfadd_s },
    { 0xb8000800u, 0xfc0019c0u, trans_fmpyfadd_d }
};

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
    case 0x00: /* system op */
        return translate_table(ctx, insn, table_system);
    case 0x01:
        return translate_table(ctx, insn, table_mem_mgmt);
    case 0x02:
        return translate_table(ctx, insn, table_arith_log);
    case 0x03:
        return translate_table(ctx, insn, table_index_mem);
    case 0x06:
        return trans_fmpyadd(ctx, insn, false);
    case 0x08:
        return trans_ldil(ctx, insn);
    case 0x09:
        return trans_copr_w(ctx, insn);
    case 0x0A:
        return trans_addil(ctx, insn);
    case 0x0B:
        return trans_copr_dw(ctx, insn);
    case 0x0C:
        return translate_table(ctx, insn, table_float_0c);
    case 0x0D:
        return trans_ldo(ctx, insn);
    case 0x0E:
        return translate_table(ctx, insn, table_float_0e);

    case 0x10:
        return trans_load(ctx, insn, false, MO_UB);
    case 0x11:
        return trans_load(ctx, insn, false, MO_TEUW);
    case 0x12:
        return trans_load(ctx, insn, false, MO_TEUL);
    case 0x13:
        return trans_load(ctx, insn, true, MO_TEUL);
    case 0x16:
        return trans_fload_mod(ctx, insn);
    case 0x17:
        return trans_load_w(ctx, insn);
    case 0x18:
        return trans_store(ctx, insn, false, MO_UB);
    case 0x19:
        return trans_store(ctx, insn, false, MO_TEUW);
    case 0x1A:
        return trans_store(ctx, insn, false, MO_TEUL);
    case 0x1B:
        return trans_store(ctx, insn, true, MO_TEUL);
    case 0x1E:
        return trans_fstore_mod(ctx, insn);
    case 0x1F:
        return trans_store_w(ctx, insn);

    case 0x20:
        return trans_cmpb(ctx, insn, true, false, false);
    case 0x21:
        return trans_cmpb(ctx, insn, true, true, false);
    case 0x22:
        return trans_cmpb(ctx, insn, false, false, false);
    case 0x23:
        return trans_cmpb(ctx, insn, false, true, false);
    case 0x24:
        return trans_cmpiclr(ctx, insn);
    case 0x25:
        return trans_subi(ctx, insn);
    case 0x26:
        return trans_fmpyadd(ctx, insn, true);
    case 0x27:
        return trans_cmpb(ctx, insn, true, false, true);
    case 0x28:
        return trans_addb(ctx, insn, true, false);
    case 0x29:
        return trans_addb(ctx, insn, true, true);
    case 0x2A:
        return trans_addb(ctx, insn, false, false);
    case 0x2B:
        return trans_addb(ctx, insn, false, true);
    case 0x2C:
    case 0x2D:
        return trans_addi(ctx, insn);
    case 0x2E:
        return translate_table(ctx, insn, table_fp_fused);
    case 0x2F:
        return trans_cmpb(ctx, insn, false, false, true);

    case 0x30:
    case 0x31:
        return trans_bb(ctx, insn);
    case 0x32:
        return trans_movb(ctx, insn, false);
    case 0x33:
        return trans_movb(ctx, insn, true);
    case 0x34:
        return translate_table(ctx, insn, table_sh_ex);
    case 0x35:
        return translate_table(ctx, insn, table_depw);
    case 0x38:
        return trans_be(ctx, insn, false);
    case 0x39:
        return trans_be(ctx, insn, true);
    case 0x3A:
        return translate_table(ctx, insn, table_branch);

    case 0x04: /* spopn */
    case 0x05: /* diag */
    case 0x0F: /* product specific */
        break;

    case 0x07: /* unassigned */
    case 0x15: /* unassigned */
    case 0x1D: /* unassigned */
    case 0x37: /* unassigned */
    case 0x3F: /* unassigned */
    default:
        break;
    }
    return gen_illegal(ctx);
}

void gen_intermediate_code(CPUState *cs, struct TranslationBlock *tb)
{
    CPUHPPAState *env = cs->env_ptr;
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

        if (ctx.iaoq_f < TARGET_PAGE_SIZE) {
            ret = do_page_zero(&ctx);
            assert(ret != NO_EXIT);
        } else {
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
            tcg_gen_lookup_and_goto_ptr(cpu_iaoq_f);
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
        switch (tb->pc) {
        case 0x00:
            qemu_log("IN:\n0x00000000:  (null)\n\n");
            break;
        case 0xb0:
            qemu_log("IN:\n0x000000b0:  light-weight-syscall\n\n");
            break;
        case 0xe0:
            qemu_log("IN:\n0x000000e0:  set-thread-pointer-syscall\n\n");
            break;
        case 0x100:
            qemu_log("IN:\n0x00000100:  syscall\n\n");
            break;
        default:
            qemu_log("IN: %s\n", lookup_symbol(tb->pc));
            log_target_disas(cs, tb->pc, tb->size, 1);
            qemu_log("\n");
            break;
        }
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
