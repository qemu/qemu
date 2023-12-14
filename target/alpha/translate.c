/*
 *  Alpha emulation cpu translation for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "disas/disas.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/log.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#undef ALPHA_DEBUG_DISAS
#define CONFIG_SOFTFLOAT_INLINE

#ifdef ALPHA_DEBUG_DISAS
#  define LOG_DISAS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

typedef struct DisasContext DisasContext;
struct DisasContext {
    DisasContextBase base;

#ifdef CONFIG_USER_ONLY
    MemOp unalign;
#else
    uint64_t palbr;
#endif
    uint32_t tbflags;
    int mem_idx;

    /* implver and amask values for this CPU.  */
    int implver;
    int amask;

    /* Current rounding mode for this TB.  */
    int tb_rm;
    /* Current flush-to-zero setting for this TB.  */
    int tb_ftz;

    /* The set of registers active in the current context.  */
    TCGv *ir;

    /* Temporaries for $31 and $f31 as source and destination.  */
    TCGv zero;
    TCGv sink;
};

#ifdef CONFIG_USER_ONLY
#define UNALIGN(C)  (C)->unalign
#else
#define UNALIGN(C)  MO_ALIGN
#endif

/* Target-specific return values from translate_one, indicating the
   state of the TB.  Note that DISAS_NEXT indicates that we are not
   exiting the TB.  */
#define DISAS_PC_UPDATED_NOCHAIN  DISAS_TARGET_0
#define DISAS_PC_UPDATED          DISAS_TARGET_1
#define DISAS_PC_STALE            DISAS_TARGET_2

/* global register indexes */
static TCGv cpu_std_ir[31];
static TCGv cpu_fir[31];
static TCGv cpu_pc;
static TCGv cpu_lock_addr;
static TCGv cpu_lock_value;

#ifndef CONFIG_USER_ONLY
static TCGv cpu_pal_ir[31];
#endif

void alpha_translate_init(void)
{
#define DEF_VAR(V)  { &cpu_##V, #V, offsetof(CPUAlphaState, V) }

    typedef struct { TCGv *var; const char *name; int ofs; } GlobalVar;
    static const GlobalVar vars[] = {
        DEF_VAR(pc),
        DEF_VAR(lock_addr),
        DEF_VAR(lock_value),
    };

#undef DEF_VAR

    /* Use the symbolic register names that match the disassembler.  */
    static const char greg_names[31][4] = {
        "v0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "t7", "s0", "s1", "s2", "s3", "s4", "s5", "fp",
        "a0", "a1", "a2", "a3", "a4", "a5", "t8", "t9",
        "t10", "t11", "ra", "t12", "at", "gp", "sp"
    };
    static const char freg_names[31][4] = {
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30"
    };
#ifndef CONFIG_USER_ONLY
    static const char shadow_names[8][8] = {
        "pal_t7", "pal_s0", "pal_s1", "pal_s2",
        "pal_s3", "pal_s4", "pal_s5", "pal_t11"
    };
#endif

    int i;

    for (i = 0; i < 31; i++) {
        cpu_std_ir[i] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUAlphaState, ir[i]),
                                               greg_names[i]);
    }

    for (i = 0; i < 31; i++) {
        cpu_fir[i] = tcg_global_mem_new_i64(tcg_env,
                                            offsetof(CPUAlphaState, fir[i]),
                                            freg_names[i]);
    }

#ifndef CONFIG_USER_ONLY
    memcpy(cpu_pal_ir, cpu_std_ir, sizeof(cpu_pal_ir));
    for (i = 0; i < 8; i++) {
        int r = (i == 7 ? 25 : i + 8);
        cpu_pal_ir[r] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUAlphaState,
                                                        shadow[i]),
                                               shadow_names[i]);
    }
#endif

    for (i = 0; i < ARRAY_SIZE(vars); ++i) {
        const GlobalVar *v = &vars[i];
        *v->var = tcg_global_mem_new_i64(tcg_env, v->ofs, v->name);
    }
}

static TCGv load_zero(DisasContext *ctx)
{
    if (!ctx->zero) {
        ctx->zero = tcg_constant_i64(0);
    }
    return ctx->zero;
}

static TCGv dest_sink(DisasContext *ctx)
{
    if (!ctx->sink) {
        ctx->sink = tcg_temp_new();
    }
    return ctx->sink;
}

static void free_context_temps(DisasContext *ctx)
{
    if (ctx->sink) {
        tcg_gen_discard_i64(ctx->sink);
        ctx->sink = NULL;
    }
}

static TCGv load_gpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv load_gpr_lit(DisasContext *ctx, unsigned reg,
                         uint8_t lit, bool islit)
{
    if (islit) {
        return tcg_constant_i64(lit);
    } else if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv dest_gpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return dest_sink(ctx);
    }
}

static TCGv load_fpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return cpu_fir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv dest_fpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return cpu_fir[reg];
    } else {
        return dest_sink(ctx);
    }
}

static int get_flag_ofs(unsigned shift)
{
    int ofs = offsetof(CPUAlphaState, flags);
#if HOST_BIG_ENDIAN
    ofs += 3 - (shift / 8);
#else
    ofs += shift / 8;
#endif
    return ofs;
}

static void ld_flag_byte(TCGv val, unsigned shift)
{
    tcg_gen_ld8u_i64(val, tcg_env, get_flag_ofs(shift));
}

static void st_flag_byte(TCGv val, unsigned shift)
{
    tcg_gen_st8_i64(val, tcg_env, get_flag_ofs(shift));
}

static void gen_excp_1(int exception, int error_code)
{
    TCGv_i32 tmp1, tmp2;

    tmp1 = tcg_constant_i32(exception);
    tmp2 = tcg_constant_i32(error_code);
    gen_helper_excp(tcg_env, tmp1, tmp2);
}

static DisasJumpType gen_excp(DisasContext *ctx, int exception, int error_code)
{
    tcg_gen_movi_i64(cpu_pc, ctx->base.pc_next);
    gen_excp_1(exception, error_code);
    return DISAS_NORETURN;
}

static inline DisasJumpType gen_invalid(DisasContext *ctx)
{
    return gen_excp(ctx, EXCP_OPCDEC, 0);
}

static void gen_ldf(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
    gen_helper_memory_to_f(dest, tmp32);
}

static void gen_ldg(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_qemu_ld_i64(tmp, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
    gen_helper_memory_to_g(dest, tmp);
}

static void gen_lds(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
    gen_helper_memory_to_s(dest, tmp32);
}

static void gen_ldt(DisasContext *ctx, TCGv dest, TCGv addr)
{
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_load_fp(DisasContext *ctx, int ra, int rb, int32_t disp16,
                        void (*func)(DisasContext *, TCGv, TCGv))
{
    /* Loads to $f31 are prefetches, which we can treat as nops. */
    if (likely(ra != 31)) {
        TCGv addr = tcg_temp_new();
        tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
        func(ctx, cpu_fir[ra], addr);
    }
}

static void gen_load_int(DisasContext *ctx, int ra, int rb, int32_t disp16,
                         MemOp op, bool clear, bool locked)
{
    TCGv addr, dest;

    /* LDQ_U with ra $31 is UNOP.  Other various loads are forms of
       prefetches, which we can treat as nops.  No worries about
       missed exceptions here.  */
    if (unlikely(ra == 31)) {
        return;
    }

    addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    if (clear) {
        tcg_gen_andi_i64(addr, addr, ~0x7);
    } else if (!locked) {
        op |= UNALIGN(ctx);
    }

    dest = ctx->ir[ra];
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, op);

    if (locked) {
        tcg_gen_mov_i64(cpu_lock_addr, addr);
        tcg_gen_mov_i64(cpu_lock_value, dest);
    }
}

static void gen_stf(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    gen_helper_f_to_memory(tmp32, addr);
    tcg_gen_qemu_st_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
}

static void gen_stg(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    gen_helper_g_to_memory(tmp, src);
    tcg_gen_qemu_st_i64(tmp, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_sts(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    gen_helper_s_to_memory(tmp32, src);
    tcg_gen_qemu_st_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
}

static void gen_stt(DisasContext *ctx, TCGv src, TCGv addr)
{
    tcg_gen_qemu_st_i64(src, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_store_fp(DisasContext *ctx, int ra, int rb, int32_t disp16,
                         void (*func)(DisasContext *, TCGv, TCGv))
{
    TCGv addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    func(ctx, load_fpr(ctx, ra), addr);
}

static void gen_store_int(DisasContext *ctx, int ra, int rb, int32_t disp16,
                          MemOp op, bool clear)
{
    TCGv addr, src;

    addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    if (clear) {
        tcg_gen_andi_i64(addr, addr, ~0x7);
    } else {
        op |= UNALIGN(ctx);
    }

    src = load_gpr(ctx, ra);
    tcg_gen_qemu_st_i64(src, addr, ctx->mem_idx, op);
}

static DisasJumpType gen_store_conditional(DisasContext *ctx, int ra, int rb,
                                           int32_t disp16, int mem_idx,
                                           MemOp op)
{
    TCGLabel *lab_fail, *lab_done;
    TCGv addr, val;

    addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    free_context_temps(ctx);

    lab_fail = gen_new_label();
    lab_done = gen_new_label();
    tcg_gen_brcond_i64(TCG_COND_NE, addr, cpu_lock_addr, lab_fail);

    val = tcg_temp_new_i64();
    tcg_gen_atomic_cmpxchg_i64(val, cpu_lock_addr, cpu_lock_value,
                               load_gpr(ctx, ra), mem_idx, op);
    free_context_temps(ctx);

    if (ra != 31) {
        tcg_gen_setcond_i64(TCG_COND_EQ, ctx->ir[ra], val, cpu_lock_value);
    }
    tcg_gen_br(lab_done);

    gen_set_label(lab_fail);
    if (ra != 31) {
        tcg_gen_movi_i64(ctx->ir[ra], 0);
    }

    gen_set_label(lab_done);
    tcg_gen_movi_i64(cpu_lock_addr, -1);
    return DISAS_NEXT;
}

static bool use_goto_tb(DisasContext *ctx, uint64_t dest)
{
    return translator_use_goto_tb(&ctx->base, dest);
}

static DisasJumpType gen_bdirect(DisasContext *ctx, int ra, int32_t disp)
{
    uint64_t dest = ctx->base.pc_next + (disp << 2);

    if (ra != 31) {
        tcg_gen_movi_i64(ctx->ir[ra], ctx->base.pc_next);
    }

    /* Notice branch-to-next; used to initialize RA with the PC.  */
    if (disp == 0) {
        return 0;
    } else if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, 0);
        return DISAS_NORETURN;
    } else {
        tcg_gen_movi_i64(cpu_pc, dest);
        return DISAS_PC_UPDATED;
    }
}

static DisasJumpType gen_bcond_internal(DisasContext *ctx, TCGCond cond,
                                        TCGv cmp, int32_t disp)
{
    uint64_t dest = ctx->base.pc_next + (disp << 2);
    TCGLabel *lab_true = gen_new_label();

    if (use_goto_tb(ctx, dest)) {
        tcg_gen_brcondi_i64(cond, cmp, 0, lab_true);

        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(cpu_pc, ctx->base.pc_next);
        tcg_gen_exit_tb(ctx->base.tb, 0);

        gen_set_label(lab_true);
        tcg_gen_goto_tb(1);
        tcg_gen_movi_i64(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, 1);

        return DISAS_NORETURN;
    } else {
        TCGv_i64 z = load_zero(ctx);
        TCGv_i64 d = tcg_constant_i64(dest);
        TCGv_i64 p = tcg_constant_i64(ctx->base.pc_next);

        tcg_gen_movcond_i64(cond, cpu_pc, cmp, z, d, p);
        return DISAS_PC_UPDATED;
    }
}

static DisasJumpType gen_bcond(DisasContext *ctx, TCGCond cond, int ra,
                               int32_t disp, int mask)
{
    if (mask) {
        TCGv tmp = tcg_temp_new();
        DisasJumpType ret;

        tcg_gen_andi_i64(tmp, load_gpr(ctx, ra), 1);
        ret = gen_bcond_internal(ctx, cond, tmp, disp);
        return ret;
    }
    return gen_bcond_internal(ctx, cond, load_gpr(ctx, ra), disp);
}

/* Fold -0.0 for comparison with COND.  */

static void gen_fold_mzero(TCGCond cond, TCGv dest, TCGv src)
{
    uint64_t mzero = 1ull << 63;

    switch (cond) {
    case TCG_COND_LE:
    case TCG_COND_GT:
        /* For <= or >, the -0.0 value directly compares the way we want.  */
        tcg_gen_mov_i64(dest, src);
        break;

    case TCG_COND_EQ:
    case TCG_COND_NE:
        /* For == or !=, we can simply mask off the sign bit and compare.  */
        tcg_gen_andi_i64(dest, src, mzero - 1);
        break;

    case TCG_COND_GE:
    case TCG_COND_LT:
        /* For >= or <, map -0.0 to +0.0. */
        tcg_gen_movcond_i64(TCG_COND_NE, dest, src, tcg_constant_i64(mzero),
                            src, tcg_constant_i64(0));
        break;

    default:
        abort();
    }
}

static DisasJumpType gen_fbcond(DisasContext *ctx, TCGCond cond, int ra,
                                int32_t disp)
{
    TCGv cmp_tmp = tcg_temp_new();
    DisasJumpType ret;

    gen_fold_mzero(cond, cmp_tmp, load_fpr(ctx, ra));
    ret = gen_bcond_internal(ctx, cond, cmp_tmp, disp);
    return ret;
}

static void gen_fcmov(DisasContext *ctx, TCGCond cond, int ra, int rb, int rc)
{
    TCGv_i64 va, vb, z;

    z = load_zero(ctx);
    vb = load_fpr(ctx, rb);
    va = tcg_temp_new();
    gen_fold_mzero(cond, va, load_fpr(ctx, ra));

    tcg_gen_movcond_i64(cond, dest_fpr(ctx, rc), va, z, vb, load_fpr(ctx, rc));
}

#define QUAL_RM_N       0x080   /* Round mode nearest even */
#define QUAL_RM_C       0x000   /* Round mode chopped */
#define QUAL_RM_M       0x040   /* Round mode minus infinity */
#define QUAL_RM_D       0x0c0   /* Round mode dynamic */
#define QUAL_RM_MASK    0x0c0

#define QUAL_U          0x100   /* Underflow enable (fp output) */
#define QUAL_V          0x100   /* Overflow enable (int output) */
#define QUAL_S          0x400   /* Software completion enable */
#define QUAL_I          0x200   /* Inexact detection enable */

static void gen_qual_roundmode(DisasContext *ctx, int fn11)
{
    TCGv_i32 tmp;

    fn11 &= QUAL_RM_MASK;
    if (fn11 == ctx->tb_rm) {
        return;
    }
    ctx->tb_rm = fn11;

    tmp = tcg_temp_new_i32();
    switch (fn11) {
    case QUAL_RM_N:
        tcg_gen_movi_i32(tmp, float_round_nearest_even);
        break;
    case QUAL_RM_C:
        tcg_gen_movi_i32(tmp, float_round_to_zero);
        break;
    case QUAL_RM_M:
        tcg_gen_movi_i32(tmp, float_round_down);
        break;
    case QUAL_RM_D:
        tcg_gen_ld8u_i32(tmp, tcg_env,
                         offsetof(CPUAlphaState, fpcr_dyn_round));
        break;
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    /* ??? The "fpu/softfloat.h" interface is to call set_float_rounding_mode.
       With CONFIG_SOFTFLOAT that expands to an out-of-line call that just
       sets the one field.  */
    tcg_gen_st8_i32(tmp, tcg_env,
                    offsetof(CPUAlphaState, fp_status.float_rounding_mode));
#else
    gen_helper_setroundmode(tmp);
#endif
}

static void gen_qual_flushzero(DisasContext *ctx, int fn11)
{
    TCGv_i32 tmp;

    fn11 &= QUAL_U;
    if (fn11 == ctx->tb_ftz) {
        return;
    }
    ctx->tb_ftz = fn11;

    tmp = tcg_temp_new_i32();
    if (fn11) {
        /* Underflow is enabled, use the FPCR setting.  */
        tcg_gen_ld8u_i32(tmp, tcg_env,
                         offsetof(CPUAlphaState, fpcr_flush_to_zero));
    } else {
        /* Underflow is disabled, force flush-to-zero.  */
        tcg_gen_movi_i32(tmp, 1);
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    tcg_gen_st8_i32(tmp, tcg_env,
                    offsetof(CPUAlphaState, fp_status.flush_to_zero));
#else
    gen_helper_setflushzero(tmp);
#endif
}

static TCGv gen_ieee_input(DisasContext *ctx, int reg, int fn11, int is_cmp)
{
    TCGv val;

    if (unlikely(reg == 31)) {
        val = load_zero(ctx);
    } else {
        val = cpu_fir[reg];
        if ((fn11 & QUAL_S) == 0) {
            if (is_cmp) {
                gen_helper_ieee_input_cmp(tcg_env, val);
            } else {
                gen_helper_ieee_input(tcg_env, val);
            }
        } else {
#ifndef CONFIG_USER_ONLY
            /* In system mode, raise exceptions for denormals like real
               hardware.  In user mode, proceed as if the OS completion
               handler is handling the denormal as per spec.  */
            gen_helper_ieee_input_s(tcg_env, val);
#endif
        }
    }
    return val;
}

static void gen_fp_exc_raise(int rc, int fn11)
{
    /* ??? We ought to be able to do something with imprecise exceptions.
       E.g. notice we're still in the trap shadow of something within the
       TB and do not generate the code to signal the exception; end the TB
       when an exception is forced to arrive, either by consumption of a
       register value or TRAPB or EXCB.  */
    TCGv_i32 reg, ign;
    uint32_t ignore = 0;

    if (!(fn11 & QUAL_U)) {
        /* Note that QUAL_U == QUAL_V, so ignore either.  */
        ignore |= FPCR_UNF | FPCR_IOV;
    }
    if (!(fn11 & QUAL_I)) {
        ignore |= FPCR_INE;
    }
    ign = tcg_constant_i32(ignore);

    /* ??? Pass in the regno of the destination so that the helper can
       set EXC_MASK, which contains a bitmask of destination registers
       that have caused arithmetic traps.  A simple userspace emulation
       does not require this.  We do need it for a guest kernel's entArith,
       or if we were to do something clever with imprecise exceptions.  */
    reg = tcg_constant_i32(rc + 32);
    if (fn11 & QUAL_S) {
        gen_helper_fp_exc_raise_s(tcg_env, ign, reg);
    } else {
        gen_helper_fp_exc_raise(tcg_env, ign, reg);
    }
}

static void gen_cvtlq(TCGv vc, TCGv vb)
{
    TCGv tmp = tcg_temp_new();

    /* The arithmetic right shift here, plus the sign-extended mask below
       yields a sign-extended result without an explicit ext32s_i64.  */
    tcg_gen_shri_i64(tmp, vb, 29);
    tcg_gen_sari_i64(vc, vb, 32);
    tcg_gen_deposit_i64(vc, vc, tmp, 0, 30);
}

static void gen_ieee_arith2(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv),
                            int rb, int rc, int fn11)
{
    TCGv vb;

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);

    vb = gen_ieee_input(ctx, rb, fn11, 0);
    helper(dest_fpr(ctx, rc), tcg_env, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH2(name)                                       \
static inline void glue(gen_, name)(DisasContext *ctx,          \
                                    int rb, int rc, int fn11)   \
{                                                               \
    gen_ieee_arith2(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_ARITH2(sqrts)
IEEE_ARITH2(sqrtt)
IEEE_ARITH2(cvtst)
IEEE_ARITH2(cvtts)

static void gen_cvttq(DisasContext *ctx, int rb, int rc, int fn11)
{
    TCGv vb, vc;

    /* No need to set flushzero, since we have an integer output.  */
    vb = gen_ieee_input(ctx, rb, fn11, 0);
    vc = dest_fpr(ctx, rc);

    /* Almost all integer conversions use cropped rounding;
       special case that.  */
    if ((fn11 & QUAL_RM_MASK) == QUAL_RM_C) {
        gen_helper_cvttq_c(vc, tcg_env, vb);
    } else {
        gen_qual_roundmode(ctx, fn11);
        gen_helper_cvttq(vc, tcg_env, vb);
    }
    gen_fp_exc_raise(rc, fn11);
}

static void gen_ieee_intcvt(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv),
                            int rb, int rc, int fn11)
{
    TCGv vb, vc;

    gen_qual_roundmode(ctx, fn11);
    vb = load_fpr(ctx, rb);
    vc = dest_fpr(ctx, rc);

    /* The only exception that can be raised by integer conversion
       is inexact.  Thus we only need to worry about exceptions when
       inexact handling is requested.  */
    if (fn11 & QUAL_I) {
        helper(vc, tcg_env, vb);
        gen_fp_exc_raise(rc, fn11);
    } else {
        helper(vc, tcg_env, vb);
    }
}

#define IEEE_INTCVT(name)                                       \
static inline void glue(gen_, name)(DisasContext *ctx,          \
                                    int rb, int rc, int fn11)   \
{                                                               \
    gen_ieee_intcvt(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_INTCVT(cvtqs)
IEEE_INTCVT(cvtqt)

static void gen_cpy_mask(TCGv vc, TCGv va, TCGv vb, bool inv_a, uint64_t mask)
{
    TCGv vmask = tcg_constant_i64(mask);
    TCGv tmp = tcg_temp_new_i64();

    if (inv_a) {
        tcg_gen_andc_i64(tmp, vmask, va);
    } else {
        tcg_gen_and_i64(tmp, va, vmask);
    }

    tcg_gen_andc_i64(vc, vb, vmask);
    tcg_gen_or_i64(vc, vc, tmp);
}

static void gen_ieee_arith3(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv, TCGv),
                            int ra, int rb, int rc, int fn11)
{
    TCGv va, vb, vc;

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);

    va = gen_ieee_input(ctx, ra, fn11, 0);
    vb = gen_ieee_input(ctx, rb, fn11, 0);
    vc = dest_fpr(ctx, rc);
    helper(vc, tcg_env, va, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH3(name)                                               \
static inline void glue(gen_, name)(DisasContext *ctx,                  \
                                    int ra, int rb, int rc, int fn11)   \
{                                                                       \
    gen_ieee_arith3(ctx, gen_helper_##name, ra, rb, rc, fn11);          \
}
IEEE_ARITH3(adds)
IEEE_ARITH3(subs)
IEEE_ARITH3(muls)
IEEE_ARITH3(divs)
IEEE_ARITH3(addt)
IEEE_ARITH3(subt)
IEEE_ARITH3(mult)
IEEE_ARITH3(divt)

static void gen_ieee_compare(DisasContext *ctx,
                             void (*helper)(TCGv, TCGv_ptr, TCGv, TCGv),
                             int ra, int rb, int rc, int fn11)
{
    TCGv va, vb, vc;

    va = gen_ieee_input(ctx, ra, fn11, 1);
    vb = gen_ieee_input(ctx, rb, fn11, 1);
    vc = dest_fpr(ctx, rc);
    helper(vc, tcg_env, va, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_CMP3(name)                                                 \
static inline void glue(gen_, name)(DisasContext *ctx,                  \
                                    int ra, int rb, int rc, int fn11)   \
{                                                                       \
    gen_ieee_compare(ctx, gen_helper_##name, ra, rb, rc, fn11);         \
}
IEEE_CMP3(cmptun)
IEEE_CMP3(cmpteq)
IEEE_CMP3(cmptlt)
IEEE_CMP3(cmptle)

static inline uint64_t zapnot_mask(uint8_t lit)
{
    uint64_t mask = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        if ((lit >> i) & 1) {
            mask |= 0xffull << (i * 8);
        }
    }
    return mask;
}

/* Implement zapnot with an immediate operand, which expands to some
   form of immediate AND.  This is a basic building block in the
   definition of many of the other byte manipulation instructions.  */
static void gen_zapnoti(TCGv dest, TCGv src, uint8_t lit)
{
    switch (lit) {
    case 0x00:
        tcg_gen_movi_i64(dest, 0);
        break;
    case 0x01:
        tcg_gen_ext8u_i64(dest, src);
        break;
    case 0x03:
        tcg_gen_ext16u_i64(dest, src);
        break;
    case 0x0f:
        tcg_gen_ext32u_i64(dest, src);
        break;
    case 0xff:
        tcg_gen_mov_i64(dest, src);
        break;
    default:
        tcg_gen_andi_i64(dest, src, zapnot_mask(lit));
        break;
    }
}

/* EXTWH, EXTLH, EXTQH */
static void gen_ext_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (64 - lit * 8) & 0x3f;
        int len = cto32(byte_mask) * 8;
        if (pos < len) {
            tcg_gen_deposit_z_i64(vc, va, pos, len - pos);
        } else {
            tcg_gen_movi_i64(vc, 0);
        }
    } else {
        TCGv tmp = tcg_temp_new();
        tcg_gen_shli_i64(tmp, load_gpr(ctx, rb), 3);
        tcg_gen_neg_i64(tmp, tmp);
        tcg_gen_andi_i64(tmp, tmp, 0x3f);
        tcg_gen_shl_i64(vc, va, tmp);
    }
    gen_zapnoti(vc, vc, byte_mask);
}

/* EXTBL, EXTWL, EXTLL, EXTQL */
static void gen_ext_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos + len >= 64) {
            len = 64 - pos;
        }
        tcg_gen_extract_i64(vc, va, pos, len);
    } else {
        TCGv tmp = tcg_temp_new();
        tcg_gen_andi_i64(tmp, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(tmp, tmp, 3);
        tcg_gen_shr_i64(vc, va, tmp);
        gen_zapnoti(vc, vc, byte_mask);
    }
}

/* INSWH, INSLH, INSQH */
static void gen_ins_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = 64 - (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos < len) {
            tcg_gen_extract_i64(vc, va, pos, len - pos);
        } else {
            tcg_gen_movi_i64(vc, 0);
        }
    } else {
        TCGv tmp = tcg_temp_new();
        TCGv shift = tcg_temp_new();

        /* The instruction description has us left-shift the byte mask
           and extract bits <15:8> and apply that zap at the end.  This
           is equivalent to simply performing the zap first and shifting
           afterward.  */
        gen_zapnoti(tmp, va, byte_mask);

        /* If (B & 7) == 0, we need to shift by 64 and leave a zero.  Do this
           portably by splitting the shift into two parts: shift_count-1 and 1.
           Arrange for the -1 by using ones-complement instead of
           twos-complement in the negation: ~(B * 8) & 63.  */

        tcg_gen_shli_i64(shift, load_gpr(ctx, rb), 3);
        tcg_gen_not_i64(shift, shift);
        tcg_gen_andi_i64(shift, shift, 0x3f);

        tcg_gen_shr_i64(vc, tmp, shift);
        tcg_gen_shri_i64(vc, vc, 1);
    }
}

/* INSBL, INSWL, INSLL, INSQL */
static void gen_ins_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos + len > 64) {
            len = 64 - pos;
        }
        tcg_gen_deposit_z_i64(vc, va, pos, len);
    } else {
        TCGv tmp = tcg_temp_new();
        TCGv shift = tcg_temp_new();

        /* The instruction description has us left-shift the byte mask
           and extract bits <15:8> and apply that zap at the end.  This
           is equivalent to simply performing the zap first and shifting
           afterward.  */
        gen_zapnoti(tmp, va, byte_mask);

        tcg_gen_andi_i64(shift, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_shl_i64(vc, tmp, shift);
    }
}

/* MSKWH, MSKLH, MSKQH */
static void gen_msk_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        gen_zapnoti(vc, va, ~((byte_mask << (lit & 7)) >> 8));
    } else {
        TCGv shift = tcg_temp_new();
        TCGv mask = tcg_temp_new();

        /* The instruction description is as above, where the byte_mask
           is shifted left, and then we extract bits <15:8>.  This can be
           emulated with a right-shift on the expanded byte mask.  This
           requires extra care because for an input <2:0> == 0 we need a
           shift of 64 bits in order to generate a zero.  This is done by
           splitting the shift into two parts, the variable shift - 1
           followed by a constant 1 shift.  The code we expand below is
           equivalent to ~(B * 8) & 63.  */

        tcg_gen_shli_i64(shift, load_gpr(ctx, rb), 3);
        tcg_gen_not_i64(shift, shift);
        tcg_gen_andi_i64(shift, shift, 0x3f);
        tcg_gen_movi_i64(mask, zapnot_mask (byte_mask));
        tcg_gen_shr_i64(mask, mask, shift);
        tcg_gen_shri_i64(mask, mask, 1);

        tcg_gen_andc_i64(vc, va, mask);
    }
}

/* MSKBL, MSKWL, MSKLL, MSKQL */
static void gen_msk_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        gen_zapnoti(vc, va, ~(byte_mask << (lit & 7)));
    } else {
        TCGv shift = tcg_temp_new();
        TCGv mask = tcg_temp_new();

        tcg_gen_andi_i64(shift, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_movi_i64(mask, zapnot_mask(byte_mask));
        tcg_gen_shl_i64(mask, mask, shift);

        tcg_gen_andc_i64(vc, va, mask);
    }
}

static void gen_rx(DisasContext *ctx, int ra, int set)
{
    if (ra != 31) {
        ld_flag_byte(ctx->ir[ra], ENV_FLAG_RX_SHIFT);
    }

    st_flag_byte(tcg_constant_i64(set), ENV_FLAG_RX_SHIFT);
}

static DisasJumpType gen_call_pal(DisasContext *ctx, int palcode)
{
    /* We're emulating OSF/1 PALcode.  Many of these are trivial access
       to internal cpu registers.  */

    /* Unprivileged PAL call */
    if (palcode >= 0x80 && palcode < 0xC0) {
        switch (palcode) {
        case 0x86:
            /* IMB */
            /* No-op inside QEMU.  */
            break;
        case 0x9E:
            /* RDUNIQUE */
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, unique));
            break;
        case 0x9F:
            /* WRUNIQUE */
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, unique));
            break;
        default:
            palcode &= 0xbf;
            goto do_call_pal;
        }
        return DISAS_NEXT;
    }

#ifndef CONFIG_USER_ONLY
    /* Privileged PAL code */
    if (palcode < 0x40 && (ctx->tbflags & ENV_FLAG_PS_USER) == 0) {
        switch (palcode) {
        case 0x01:
            /* CFLUSH */
            /* No-op inside QEMU.  */
            break;
        case 0x02:
            /* DRAINA */
            /* No-op inside QEMU.  */
            break;
        case 0x2D:
            /* WRVPTPTR */
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, vptptr));
            break;
        case 0x31:
            /* WRVAL */
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, sysval));
            break;
        case 0x32:
            /* RDVAL */
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, sysval));
            break;

        case 0x35:
            /* SWPIPL */
            /* Note that we already know we're in kernel mode, so we know
               that PS only contains the 3 IPL bits.  */
            ld_flag_byte(ctx->ir[IR_V0], ENV_FLAG_PS_SHIFT);

            /* But make sure and store only the 3 IPL bits from the user.  */
            {
                TCGv tmp = tcg_temp_new();
                tcg_gen_andi_i64(tmp, ctx->ir[IR_A0], PS_INT_MASK);
                st_flag_byte(tmp, ENV_FLAG_PS_SHIFT);
            }

            /* Allow interrupts to be recognized right away.  */
            tcg_gen_movi_i64(cpu_pc, ctx->base.pc_next);
            return DISAS_PC_UPDATED_NOCHAIN;

        case 0x36:
            /* RDPS */
            ld_flag_byte(ctx->ir[IR_V0], ENV_FLAG_PS_SHIFT);
            break;

        case 0x38:
            /* WRUSP */
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, usp));
            break;
        case 0x3A:
            /* RDUSP */
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, usp));
            break;
        case 0x3C:
            /* WHAMI */
            tcg_gen_ld32s_i64(ctx->ir[IR_V0], tcg_env,
                -offsetof(AlphaCPU, env) + offsetof(CPUState, cpu_index));
            break;

        case 0x3E:
            /* WTINT */
            tcg_gen_st_i32(tcg_constant_i32(1), tcg_env,
                           -offsetof(AlphaCPU, env) +
                           offsetof(CPUState, halted));
            tcg_gen_movi_i64(ctx->ir[IR_V0], 0);
            return gen_excp(ctx, EXCP_HALTED, 0);

        default:
            palcode &= 0x3f;
            goto do_call_pal;
        }
        return DISAS_NEXT;
    }
#endif
    return gen_invalid(ctx);

 do_call_pal:
#ifdef CONFIG_USER_ONLY
    return gen_excp(ctx, EXCP_CALL_PAL, palcode);
#else
    {
        TCGv tmp = tcg_temp_new();
        uint64_t exc_addr = ctx->base.pc_next;
        uint64_t entry = ctx->palbr;

        if (ctx->tbflags & ENV_FLAG_PAL_MODE) {
            exc_addr |= 1;
        } else {
            tcg_gen_movi_i64(tmp, 1);
            st_flag_byte(tmp, ENV_FLAG_PAL_SHIFT);
        }

        tcg_gen_movi_i64(tmp, exc_addr);
        tcg_gen_st_i64(tmp, tcg_env, offsetof(CPUAlphaState, exc_addr));

        entry += (palcode & 0x80
                  ? 0x2000 + (palcode - 0x80) * 64
                  : 0x1000 + palcode * 64);

        tcg_gen_movi_i64(cpu_pc, entry);
        return DISAS_PC_UPDATED;
    }
#endif
}

#ifndef CONFIG_USER_ONLY

#define PR_LONG         0x200000

static int cpu_pr_data(int pr)
{
    switch (pr) {
    case  2: return offsetof(CPUAlphaState, pcc_ofs) | PR_LONG;
    case  3: return offsetof(CPUAlphaState, trap_arg0);
    case  4: return offsetof(CPUAlphaState, trap_arg1);
    case  5: return offsetof(CPUAlphaState, trap_arg2);
    case  6: return offsetof(CPUAlphaState, exc_addr);
    case  7: return offsetof(CPUAlphaState, palbr);
    case  8: return offsetof(CPUAlphaState, ptbr);
    case  9: return offsetof(CPUAlphaState, vptptr);
    case 10: return offsetof(CPUAlphaState, unique);
    case 11: return offsetof(CPUAlphaState, sysval);
    case 12: return offsetof(CPUAlphaState, usp);

    case 40 ... 63:
        return offsetof(CPUAlphaState, scratch[pr - 40]);

    case 251:
        return offsetof(CPUAlphaState, alarm_expire);
    }
    return 0;
}

static DisasJumpType gen_mfpr(DisasContext *ctx, TCGv va, int regno)
{
    void (*helper)(TCGv);
    int data;

    switch (regno) {
    case 32 ... 39:
        /* Accessing the "non-shadow" general registers.  */
        regno = regno == 39 ? 25 : regno - 32 + 8;
        tcg_gen_mov_i64(va, cpu_std_ir[regno]);
        break;

    case 250: /* WALLTIME */
        helper = gen_helper_get_walltime;
        goto do_helper;
    case 249: /* VMTIME */
        helper = gen_helper_get_vmtime;
    do_helper:
        if (translator_io_start(&ctx->base)) {
            helper(va);
            return DISAS_PC_STALE;
        } else {
            helper(va);
        }
        break;

    case 0: /* PS */
        ld_flag_byte(va, ENV_FLAG_PS_SHIFT);
        break;
    case 1: /* FEN */
        ld_flag_byte(va, ENV_FLAG_FEN_SHIFT);
        break;

    default:
        /* The basic registers are data only, and unknown registers
           are read-zero, write-ignore.  */
        data = cpu_pr_data(regno);
        if (data == 0) {
            tcg_gen_movi_i64(va, 0);
        } else if (data & PR_LONG) {
            tcg_gen_ld32s_i64(va, tcg_env, data & ~PR_LONG);
        } else {
            tcg_gen_ld_i64(va, tcg_env, data);
        }
        break;
    }

    return DISAS_NEXT;
}

static DisasJumpType gen_mtpr(DisasContext *ctx, TCGv vb, int regno)
{
    int data;
    DisasJumpType ret = DISAS_NEXT;

    switch (regno) {
    case 255:
        /* TBIA */
        gen_helper_tbia(tcg_env);
        break;

    case 254:
        /* TBIS */
        gen_helper_tbis(tcg_env, vb);
        break;

    case 253:
        /* WAIT */
        tcg_gen_st_i32(tcg_constant_i32(1), tcg_env,
                       -offsetof(AlphaCPU, env) + offsetof(CPUState, halted));
        return gen_excp(ctx, EXCP_HALTED, 0);

    case 252:
        /* HALT */
        gen_helper_halt(vb);
        return DISAS_PC_STALE;

    case 251:
        /* ALARM */
        if (translator_io_start(&ctx->base)) {
            ret = DISAS_PC_STALE;
        }
        gen_helper_set_alarm(tcg_env, vb);
        break;

    case 7:
        /* PALBR */
        tcg_gen_st_i64(vb, tcg_env, offsetof(CPUAlphaState, palbr));
        /* Changing the PAL base register implies un-chaining all of the TBs
           that ended with a CALL_PAL.  Since the base register usually only
           changes during boot, flushing everything works well.  */
        gen_helper_tb_flush(tcg_env);
        return DISAS_PC_STALE;

    case 32 ... 39:
        /* Accessing the "non-shadow" general registers.  */
        regno = regno == 39 ? 25 : regno - 32 + 8;
        tcg_gen_mov_i64(cpu_std_ir[regno], vb);
        break;

    case 0: /* PS */
        st_flag_byte(vb, ENV_FLAG_PS_SHIFT);
        break;
    case 1: /* FEN */
        st_flag_byte(vb, ENV_FLAG_FEN_SHIFT);
        break;

    default:
        /* The basic registers are data only, and unknown registers
           are read-zero, write-ignore.  */
        data = cpu_pr_data(regno);
        if (data != 0) {
            if (data & PR_LONG) {
                tcg_gen_st32_i64(vb, tcg_env, data & ~PR_LONG);
            } else {
                tcg_gen_st_i64(vb, tcg_env, data);
            }
        }
        break;
    }

    return ret;
}
#endif /* !USER_ONLY*/

#define REQUIRE_NO_LIT                          \
    do {                                        \
        if (real_islit) {                       \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_AMASK(FLAG)                     \
    do {                                        \
        if ((ctx->amask & AMASK_##FLAG) == 0) { \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_TB_FLAG(FLAG)                   \
    do {                                        \
        if ((ctx->tbflags & (FLAG)) == 0) {     \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_REG_31(WHICH)                   \
    do {                                        \
        if (WHICH != 31) {                      \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_FEN                             \
    do {                                        \
        if (!(ctx->tbflags & ENV_FLAG_FEN)) {   \
            goto raise_fen;                     \
        }                                       \
    } while (0)

static DisasJumpType translate_one(DisasContext *ctx, uint32_t insn)
{
    int32_t disp21, disp16, disp12 __attribute__((unused));
    uint16_t fn11;
    uint8_t opc, ra, rb, rc, fpfn, fn7, lit;
    bool islit, real_islit;
    TCGv va, vb, vc, tmp, tmp2;
    TCGv_i32 t32;
    DisasJumpType ret;

    /* Decode all instruction fields */
    opc = extract32(insn, 26, 6);
    ra = extract32(insn, 21, 5);
    rb = extract32(insn, 16, 5);
    rc = extract32(insn, 0, 5);
    real_islit = islit = extract32(insn, 12, 1);
    lit = extract32(insn, 13, 8);

    disp21 = sextract32(insn, 0, 21);
    disp16 = sextract32(insn, 0, 16);
    disp12 = sextract32(insn, 0, 12);

    fn11 = extract32(insn, 5, 11);
    fpfn = extract32(insn, 5, 6);
    fn7 = extract32(insn, 5, 7);

    if (rb == 31 && !islit) {
        islit = true;
        lit = 0;
    }

    ret = DISAS_NEXT;
    switch (opc) {
    case 0x00:
        /* CALL_PAL */
        ret = gen_call_pal(ctx, insn & 0x03ffffff);
        break;
    case 0x01:
        /* OPC01 */
        goto invalid_opc;
    case 0x02:
        /* OPC02 */
        goto invalid_opc;
    case 0x03:
        /* OPC03 */
        goto invalid_opc;
    case 0x04:
        /* OPC04 */
        goto invalid_opc;
    case 0x05:
        /* OPC05 */
        goto invalid_opc;
    case 0x06:
        /* OPC06 */
        goto invalid_opc;
    case 0x07:
        /* OPC07 */
        goto invalid_opc;

    case 0x09:
        /* LDAH */
        disp16 = (uint32_t)disp16 << 16;
        /* fall through */
    case 0x08:
        /* LDA */
        va = dest_gpr(ctx, ra);
        /* It's worth special-casing immediate loads.  */
        if (rb == 31) {
            tcg_gen_movi_i64(va, disp16);
        } else {
            tcg_gen_addi_i64(va, load_gpr(ctx, rb), disp16);
        }
        break;

    case 0x0A:
        /* LDBU */
        REQUIRE_AMASK(BWX);
        gen_load_int(ctx, ra, rb, disp16, MO_UB, 0, 0);
        break;
    case 0x0B:
        /* LDQ_U */
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ, 1, 0);
        break;
    case 0x0C:
        /* LDWU */
        REQUIRE_AMASK(BWX);
        gen_load_int(ctx, ra, rb, disp16, MO_LEUW, 0, 0);
        break;
    case 0x0D:
        /* STW */
        REQUIRE_AMASK(BWX);
        gen_store_int(ctx, ra, rb, disp16, MO_LEUW, 0);
        break;
    case 0x0E:
        /* STB */
        REQUIRE_AMASK(BWX);
        gen_store_int(ctx, ra, rb, disp16, MO_UB, 0);
        break;
    case 0x0F:
        /* STQ_U */
        gen_store_int(ctx, ra, rb, disp16, MO_LEUQ, 1);
        break;

    case 0x10:
        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);

        if (ra == 31) {
            if (fn7 == 0x00) {
                /* Special case ADDL as SEXTL.  */
                tcg_gen_ext32s_i64(vc, vb);
                break;
            }
            if (fn7 == 0x29) {
                /* Special case SUBQ as NEGQ.  */
                tcg_gen_neg_i64(vc, vb);
                break;
            }
        }

        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            /* ADDL */
            tcg_gen_add_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x02:
            /* S4ADDL */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_add_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x09:
            /* SUBL */
            tcg_gen_sub_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x0B:
            /* S4SUBL */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_sub_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x0F:
            /* CMPBGE */
            if (ra == 31) {
                /* Special case 0 >= X as X == 0.  */
                gen_helper_cmpbe0(vc, vb);
            } else {
                gen_helper_cmpbge(vc, va, vb);
            }
            break;
        case 0x12:
            /* S8ADDL */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_add_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x1B:
            /* S8SUBL */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_sub_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x1D:
            /* CMPULT */
            tcg_gen_setcond_i64(TCG_COND_LTU, vc, va, vb);
            break;
        case 0x20:
            /* ADDQ */
            tcg_gen_add_i64(vc, va, vb);
            break;
        case 0x22:
            /* S4ADDQ */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_add_i64(vc, tmp, vb);
            break;
        case 0x29:
            /* SUBQ */
            tcg_gen_sub_i64(vc, va, vb);
            break;
        case 0x2B:
            /* S4SUBQ */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_sub_i64(vc, tmp, vb);
            break;
        case 0x2D:
            /* CMPEQ */
            tcg_gen_setcond_i64(TCG_COND_EQ, vc, va, vb);
            break;
        case 0x32:
            /* S8ADDQ */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_add_i64(vc, tmp, vb);
            break;
        case 0x3B:
            /* S8SUBQ */
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_sub_i64(vc, tmp, vb);
            break;
        case 0x3D:
            /* CMPULE */
            tcg_gen_setcond_i64(TCG_COND_LEU, vc, va, vb);
            break;
        case 0x40:
            /* ADDL/V */
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_add_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x49:
            /* SUBL/V */
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_sub_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x4D:
            /* CMPLT */
            tcg_gen_setcond_i64(TCG_COND_LT, vc, va, vb);
            break;
        case 0x60:
            /* ADDQ/V */
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_eqv_i64(tmp, va, vb);
            tcg_gen_mov_i64(tmp2, va);
            tcg_gen_add_i64(vc, va, vb);
            tcg_gen_xor_i64(tmp2, tmp2, vc);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            tcg_gen_shri_i64(tmp, tmp, 63);
            tcg_gen_movi_i64(tmp2, 0);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        case 0x69:
            /* SUBQ/V */
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_xor_i64(tmp, va, vb);
            tcg_gen_mov_i64(tmp2, va);
            tcg_gen_sub_i64(vc, va, vb);
            tcg_gen_xor_i64(tmp2, tmp2, vc);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            tcg_gen_shri_i64(tmp, tmp, 63);
            tcg_gen_movi_i64(tmp2, 0);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        case 0x6D:
            /* CMPLE */
            tcg_gen_setcond_i64(TCG_COND_LE, vc, va, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x11:
        if (fn7 == 0x20) {
            if (rc == 31) {
                /* Special case BIS as NOP.  */
                break;
            }
            if (ra == 31) {
                /* Special case BIS as MOV.  */
                vc = dest_gpr(ctx, rc);
                if (islit) {
                    tcg_gen_movi_i64(vc, lit);
                } else {
                    tcg_gen_mov_i64(vc, load_gpr(ctx, rb));
                }
                break;
            }
        }

        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);

        if (fn7 == 0x28 && ra == 31) {
            /* Special case ORNOT as NOT.  */
            tcg_gen_not_i64(vc, vb);
            break;
        }

        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            /* AND */
            tcg_gen_and_i64(vc, va, vb);
            break;
        case 0x08:
            /* BIC */
            tcg_gen_andc_i64(vc, va, vb);
            break;
        case 0x14:
            /* CMOVLBS */
            tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, va, 1);
            tcg_gen_movcond_i64(TCG_COND_NE, vc, tmp, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x16:
            /* CMOVLBC */
            tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, va, 1);
            tcg_gen_movcond_i64(TCG_COND_EQ, vc, tmp, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x20:
            /* BIS */
            tcg_gen_or_i64(vc, va, vb);
            break;
        case 0x24:
            /* CMOVEQ */
            tcg_gen_movcond_i64(TCG_COND_EQ, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x26:
            /* CMOVNE */
            tcg_gen_movcond_i64(TCG_COND_NE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x28:
            /* ORNOT */
            tcg_gen_orc_i64(vc, va, vb);
            break;
        case 0x40:
            /* XOR */
            tcg_gen_xor_i64(vc, va, vb);
            break;
        case 0x44:
            /* CMOVLT */
            tcg_gen_movcond_i64(TCG_COND_LT, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x46:
            /* CMOVGE */
            tcg_gen_movcond_i64(TCG_COND_GE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x48:
            /* EQV */
            tcg_gen_eqv_i64(vc, va, vb);
            break;
        case 0x61:
            /* AMASK */
            REQUIRE_REG_31(ra);
            tcg_gen_andi_i64(vc, vb, ~ctx->amask);
            break;
        case 0x64:
            /* CMOVLE */
            tcg_gen_movcond_i64(TCG_COND_LE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x66:
            /* CMOVGT */
            tcg_gen_movcond_i64(TCG_COND_GT, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x6C:
            /* IMPLVER */
            REQUIRE_REG_31(ra);
            tcg_gen_movi_i64(vc, ctx->implver);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x12:
        vc = dest_gpr(ctx, rc);
        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x02:
            /* MSKBL */
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x06:
            /* EXTBL */
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x0B:
            /* INSBL */
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x12:
            /* MSKWL */
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x16:
            /* EXTWL */
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x1B:
            /* INSWL */
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x22:
            /* MSKLL */
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x26:
            /* EXTLL */
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x2B:
            /* INSLL */
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x30:
            /* ZAP */
            if (islit) {
                gen_zapnoti(vc, va, ~lit);
            } else {
                gen_helper_zap(vc, va, load_gpr(ctx, rb));
            }
            break;
        case 0x31:
            /* ZAPNOT */
            if (islit) {
                gen_zapnoti(vc, va, lit);
            } else {
                gen_helper_zapnot(vc, va, load_gpr(ctx, rb));
            }
            break;
        case 0x32:
            /* MSKQL */
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x34:
            /* SRL */
            if (islit) {
                tcg_gen_shri_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_shr_i64(vc, va, tmp);
            }
            break;
        case 0x36:
            /* EXTQL */
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x39:
            /* SLL */
            if (islit) {
                tcg_gen_shli_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_shl_i64(vc, va, tmp);
            }
            break;
        case 0x3B:
            /* INSQL */
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x3C:
            /* SRA */
            if (islit) {
                tcg_gen_sari_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_sar_i64(vc, va, tmp);
            }
            break;
        case 0x52:
            /* MSKWH */
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x57:
            /* INSWH */
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x5A:
            /* EXTWH */
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x62:
            /* MSKLH */
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x67:
            /* INSLH */
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x6A:
            /* EXTLH */
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x72:
            /* MSKQH */
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x77:
            /* INSQH */
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x7A:
            /* EXTQH */
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x13:
        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);
        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            /* MULL */
            tcg_gen_mul_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x20:
            /* MULQ */
            tcg_gen_mul_i64(vc, va, vb);
            break;
        case 0x30:
            /* UMULH */
            tmp = tcg_temp_new();
            tcg_gen_mulu2_i64(tmp, vc, va, vb);
            break;
        case 0x40:
            /* MULL/V */
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_mul_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x60:
            /* MULQ/V */
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_muls2_i64(vc, tmp, va, vb);
            tcg_gen_sari_i64(tmp2, vc, 63);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x14:
        REQUIRE_AMASK(FIX);
        vc = dest_fpr(ctx, rc);
        switch (fpfn) { /* fn11 & 0x3F */
        case 0x04:
            /* ITOFS */
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            t32 = tcg_temp_new_i32();
            va = load_gpr(ctx, ra);
            tcg_gen_extrl_i64_i32(t32, va);
            gen_helper_memory_to_s(vc, t32);
            break;
        case 0x0A:
            /* SQRTF */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vb = load_fpr(ctx, rb);
            gen_helper_sqrtf(vc, tcg_env, vb);
            break;
        case 0x0B:
            /* SQRTS */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_sqrts(ctx, rb, rc, fn11);
            break;
        case 0x14:
            /* ITOFF */
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            t32 = tcg_temp_new_i32();
            va = load_gpr(ctx, ra);
            tcg_gen_extrl_i64_i32(t32, va);
            gen_helper_memory_to_f(vc, t32);
            break;
        case 0x24:
            /* ITOFT */
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            va = load_gpr(ctx, ra);
            tcg_gen_mov_i64(vc, va);
            break;
        case 0x2A:
            /* SQRTG */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vb = load_fpr(ctx, rb);
            gen_helper_sqrtg(vc, tcg_env, vb);
            break;
        case 0x02B:
            /* SQRTT */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_sqrtt(ctx, rb, rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x15:
        /* VAX floating point */
        /* XXX: rounding mode and trap are ignored (!) */
        vc = dest_fpr(ctx, rc);
        vb = load_fpr(ctx, rb);
        va = load_fpr(ctx, ra);
        switch (fpfn) { /* fn11 & 0x3F */
        case 0x00:
            /* ADDF */
            REQUIRE_FEN;
            gen_helper_addf(vc, tcg_env, va, vb);
            break;
        case 0x01:
            /* SUBF */
            REQUIRE_FEN;
            gen_helper_subf(vc, tcg_env, va, vb);
            break;
        case 0x02:
            /* MULF */
            REQUIRE_FEN;
            gen_helper_mulf(vc, tcg_env, va, vb);
            break;
        case 0x03:
            /* DIVF */
            REQUIRE_FEN;
            gen_helper_divf(vc, tcg_env, va, vb);
            break;
        case 0x1E:
            /* CVTDG -- TODO */
            REQUIRE_REG_31(ra);
            goto invalid_opc;
        case 0x20:
            /* ADDG */
            REQUIRE_FEN;
            gen_helper_addg(vc, tcg_env, va, vb);
            break;
        case 0x21:
            /* SUBG */
            REQUIRE_FEN;
            gen_helper_subg(vc, tcg_env, va, vb);
            break;
        case 0x22:
            /* MULG */
            REQUIRE_FEN;
            gen_helper_mulg(vc, tcg_env, va, vb);
            break;
        case 0x23:
            /* DIVG */
            REQUIRE_FEN;
            gen_helper_divg(vc, tcg_env, va, vb);
            break;
        case 0x25:
            /* CMPGEQ */
            REQUIRE_FEN;
            gen_helper_cmpgeq(vc, tcg_env, va, vb);
            break;
        case 0x26:
            /* CMPGLT */
            REQUIRE_FEN;
            gen_helper_cmpglt(vc, tcg_env, va, vb);
            break;
        case 0x27:
            /* CMPGLE */
            REQUIRE_FEN;
            gen_helper_cmpgle(vc, tcg_env, va, vb);
            break;
        case 0x2C:
            /* CVTGF */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtgf(vc, tcg_env, vb);
            break;
        case 0x2D:
            /* CVTGD -- TODO */
            REQUIRE_REG_31(ra);
            goto invalid_opc;
        case 0x2F:
            /* CVTGQ */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtgq(vc, tcg_env, vb);
            break;
        case 0x3C:
            /* CVTQF */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtqf(vc, tcg_env, vb);
            break;
        case 0x3E:
            /* CVTQG */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtqg(vc, tcg_env, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x16:
        /* IEEE floating-point */
        switch (fpfn) { /* fn11 & 0x3F */
        case 0x00:
            /* ADDS */
            REQUIRE_FEN;
            gen_adds(ctx, ra, rb, rc, fn11);
            break;
        case 0x01:
            /* SUBS */
            REQUIRE_FEN;
            gen_subs(ctx, ra, rb, rc, fn11);
            break;
        case 0x02:
            /* MULS */
            REQUIRE_FEN;
            gen_muls(ctx, ra, rb, rc, fn11);
            break;
        case 0x03:
            /* DIVS */
            REQUIRE_FEN;
            gen_divs(ctx, ra, rb, rc, fn11);
            break;
        case 0x20:
            /* ADDT */
            REQUIRE_FEN;
            gen_addt(ctx, ra, rb, rc, fn11);
            break;
        case 0x21:
            /* SUBT */
            REQUIRE_FEN;
            gen_subt(ctx, ra, rb, rc, fn11);
            break;
        case 0x22:
            /* MULT */
            REQUIRE_FEN;
            gen_mult(ctx, ra, rb, rc, fn11);
            break;
        case 0x23:
            /* DIVT */
            REQUIRE_FEN;
            gen_divt(ctx, ra, rb, rc, fn11);
            break;
        case 0x24:
            /* CMPTUN */
            REQUIRE_FEN;
            gen_cmptun(ctx, ra, rb, rc, fn11);
            break;
        case 0x25:
            /* CMPTEQ */
            REQUIRE_FEN;
            gen_cmpteq(ctx, ra, rb, rc, fn11);
            break;
        case 0x26:
            /* CMPTLT */
            REQUIRE_FEN;
            gen_cmptlt(ctx, ra, rb, rc, fn11);
            break;
        case 0x27:
            /* CMPTLE */
            REQUIRE_FEN;
            gen_cmptle(ctx, ra, rb, rc, fn11);
            break;
        case 0x2C:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            if (fn11 == 0x2AC || fn11 == 0x6AC) {
                /* CVTST */
                gen_cvtst(ctx, rb, rc, fn11);
            } else {
                /* CVTTS */
                gen_cvtts(ctx, rb, rc, fn11);
            }
            break;
        case 0x2F:
            /* CVTTQ */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvttq(ctx, rb, rc, fn11);
            break;
        case 0x3C:
            /* CVTQS */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvtqs(ctx, rb, rc, fn11);
            break;
        case 0x3E:
            /* CVTQT */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvtqt(ctx, rb, rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x17:
        switch (fn11) {
        case 0x010:
            /* CVTLQ */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            gen_cvtlq(vc, vb);
            break;
        case 0x020:
            /* CPYS */
            REQUIRE_FEN;
            if (rc == 31) {
                /* Special case CPYS as FNOP.  */
            } else {
                vc = dest_fpr(ctx, rc);
                va = load_fpr(ctx, ra);
                if (ra == rb) {
                    /* Special case CPYS as FMOV.  */
                    tcg_gen_mov_i64(vc, va);
                } else {
                    vb = load_fpr(ctx, rb);
                    gen_cpy_mask(vc, va, vb, 0, 0x8000000000000000ULL);
                }
            }
            break;
        case 0x021:
            /* CPYSN */
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            va = load_fpr(ctx, ra);
            gen_cpy_mask(vc, va, vb, 1, 0x8000000000000000ULL);
            break;
        case 0x022:
            /* CPYSE */
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            va = load_fpr(ctx, ra);
            gen_cpy_mask(vc, va, vb, 0, 0xFFF0000000000000ULL);
            break;
        case 0x024:
            /* MT_FPCR */
            REQUIRE_FEN;
            va = load_fpr(ctx, ra);
            gen_helper_store_fpcr(tcg_env, va);
            if (ctx->tb_rm == QUAL_RM_D) {
                /* Re-do the copy of the rounding mode to fp_status
                   the next time we use dynamic rounding.  */
                ctx->tb_rm = -1;
            }
            break;
        case 0x025:
            /* MF_FPCR */
            REQUIRE_FEN;
            va = dest_fpr(ctx, ra);
            gen_helper_load_fpcr(va, tcg_env);
            break;
        case 0x02A:
            /* FCMOVEQ */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_EQ, ra, rb, rc);
            break;
        case 0x02B:
            /* FCMOVNE */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_NE, ra, rb, rc);
            break;
        case 0x02C:
            /* FCMOVLT */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_LT, ra, rb, rc);
            break;
        case 0x02D:
            /* FCMOVGE */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_GE, ra, rb, rc);
            break;
        case 0x02E:
            /* FCMOVLE */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_LE, ra, rb, rc);
            break;
        case 0x02F:
            /* FCMOVGT */
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_GT, ra, rb, rc);
            break;
        case 0x030: /* CVTQL */
        case 0x130: /* CVTQL/V */
        case 0x530: /* CVTQL/SV */
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            gen_helper_cvtql(vc, tcg_env, vb);
            gen_fp_exc_raise(rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x18:
        switch ((uint16_t)disp16) {
        case 0x0000:
            /* TRAPB */
            /* No-op.  */
            break;
        case 0x0400:
            /* EXCB */
            /* No-op.  */
            break;
        case 0x4000:
            /* MB */
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
            break;
        case 0x4400:
            /* WMB */
            tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
            break;
        case 0x8000:
            /* FETCH */
            /* No-op */
            break;
        case 0xA000:
            /* FETCH_M */
            /* No-op */
            break;
        case 0xC000:
            /* RPCC */
            va = dest_gpr(ctx, ra);
            if (translator_io_start(&ctx->base)) {
                ret = DISAS_PC_STALE;
            }
            gen_helper_load_pcc(va, tcg_env);
            break;
        case 0xE000:
            /* RC */
            gen_rx(ctx, ra, 0);
            break;
        case 0xE800:
            /* ECB */
            break;
        case 0xF000:
            /* RS */
            gen_rx(ctx, ra, 1);
            break;
        case 0xF800:
            /* WH64 */
            /* No-op */
            break;
        case 0xFC00:
            /* WH64EN */
            /* No-op */
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x19:
        /* HW_MFPR (PALcode) */
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        va = dest_gpr(ctx, ra);
        ret = gen_mfpr(ctx, va, insn & 0xffff);
        break;
#else
        goto invalid_opc;
#endif

    case 0x1A:
        /* JMP, JSR, RET, JSR_COROUTINE.  These only differ by the branch
           prediction stack action, which of course we don't implement.  */
        vb = load_gpr(ctx, rb);
        tcg_gen_andi_i64(cpu_pc, vb, ~3);
        if (ra != 31) {
            tcg_gen_movi_i64(ctx->ir[ra], ctx->base.pc_next);
        }
        ret = DISAS_PC_UPDATED;
        break;

    case 0x1B:
        /* HW_LD (PALcode) */
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        {
            TCGv addr = tcg_temp_new();
            vb = load_gpr(ctx, rb);
            va = dest_gpr(ctx, ra);

            tcg_gen_addi_i64(addr, vb, disp12);
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access (hw_ldl/p) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x1:
                /* Quadword physical access (hw_ldq/p) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x2:
                /* Longword physical access with lock (hw_ldl_l/p) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                tcg_gen_mov_i64(cpu_lock_addr, addr);
                tcg_gen_mov_i64(cpu_lock_value, va);
                break;
            case 0x3:
                /* Quadword physical access with lock (hw_ldq_l/p) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                tcg_gen_mov_i64(cpu_lock_addr, addr);
                tcg_gen_mov_i64(cpu_lock_value, va);
                break;
            case 0x4:
                /* Longword virtual PTE fetch (hw_ldl/v) */
                goto invalid_opc;
            case 0x5:
                /* Quadword virtual PTE fetch (hw_ldq/v) */
                goto invalid_opc;
                break;
            case 0x6:
                /* Invalid */
                goto invalid_opc;
            case 0x7:
                /* Invaliid */
                goto invalid_opc;
            case 0x8:
                /* Longword virtual access (hw_ldl) */
                goto invalid_opc;
            case 0x9:
                /* Quadword virtual access (hw_ldq) */
                goto invalid_opc;
            case 0xA:
                /* Longword virtual access with protection check (hw_ldl/w) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_KERNEL_IDX,
                                    MO_LESL | MO_ALIGN);
                break;
            case 0xB:
                /* Quadword virtual access with protection check (hw_ldq/w) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_KERNEL_IDX,
                                    MO_LEUQ | MO_ALIGN);
                break;
            case 0xC:
                /* Longword virtual access with alt access mode (hw_ldl/a)*/
                goto invalid_opc;
            case 0xD:
                /* Quadword virtual access with alt access mode (hw_ldq/a) */
                goto invalid_opc;
            case 0xE:
                /* Longword virtual access with alternate access mode and
                   protection checks (hw_ldl/wa) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_USER_IDX,
                                    MO_LESL | MO_ALIGN);
                break;
            case 0xF:
                /* Quadword virtual access with alternate access mode and
                   protection checks (hw_ldq/wa) */
                tcg_gen_qemu_ld_i64(va, addr, MMU_USER_IDX,
                                    MO_LEUQ | MO_ALIGN);
                break;
            }
            break;
        }
#else
        goto invalid_opc;
#endif

    case 0x1C:
        vc = dest_gpr(ctx, rc);
        if (fn7 == 0x70) {
            /* FTOIT */
            REQUIRE_AMASK(FIX);
            REQUIRE_REG_31(rb);
            va = load_fpr(ctx, ra);
            tcg_gen_mov_i64(vc, va);
            break;
        } else if (fn7 == 0x78) {
            /* FTOIS */
            REQUIRE_AMASK(FIX);
            REQUIRE_REG_31(rb);
            t32 = tcg_temp_new_i32();
            va = load_fpr(ctx, ra);
            gen_helper_s_to_memory(t32, va);
            tcg_gen_ext_i32_i64(vc, t32);
            break;
        }

        vb = load_gpr_lit(ctx, rb, lit, islit);
        switch (fn7) {
        case 0x00:
            /* SEXTB */
            REQUIRE_AMASK(BWX);
            REQUIRE_REG_31(ra);
            tcg_gen_ext8s_i64(vc, vb);
            break;
        case 0x01:
            /* SEXTW */
            REQUIRE_AMASK(BWX);
            REQUIRE_REG_31(ra);
            tcg_gen_ext16s_i64(vc, vb);
            break;
        case 0x30:
            /* CTPOP */
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_ctpop_i64(vc, vb);
            break;
        case 0x31:
            /* PERR */
            REQUIRE_AMASK(MVI);
            REQUIRE_NO_LIT;
            va = load_gpr(ctx, ra);
            gen_helper_perr(vc, va, vb);
            break;
        case 0x32:
            /* CTLZ */
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_clzi_i64(vc, vb, 64);
            break;
        case 0x33:
            /* CTTZ */
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_ctzi_i64(vc, vb, 64);
            break;
        case 0x34:
            /* UNPKBW */
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_unpkbw(vc, vb);
            break;
        case 0x35:
            /* UNPKBL */
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_unpkbl(vc, vb);
            break;
        case 0x36:
            /* PKWB */
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_pkwb(vc, vb);
            break;
        case 0x37:
            /* PKLB */
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_pklb(vc, vb);
            break;
        case 0x38:
            /* MINSB8 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minsb8(vc, va, vb);
            break;
        case 0x39:
            /* MINSW4 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minsw4(vc, va, vb);
            break;
        case 0x3A:
            /* MINUB8 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minub8(vc, va, vb);
            break;
        case 0x3B:
            /* MINUW4 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minuw4(vc, va, vb);
            break;
        case 0x3C:
            /* MAXUB8 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxub8(vc, va, vb);
            break;
        case 0x3D:
            /* MAXUW4 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxuw4(vc, va, vb);
            break;
        case 0x3E:
            /* MAXSB8 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxsb8(vc, va, vb);
            break;
        case 0x3F:
            /* MAXSW4 */
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxsw4(vc, va, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x1D:
        /* HW_MTPR (PALcode) */
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        vb = load_gpr(ctx, rb);
        ret = gen_mtpr(ctx, vb, insn & 0xffff);
        break;
#else
        goto invalid_opc;
#endif

    case 0x1E:
        /* HW_RET (PALcode) */
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        if (rb == 31) {
            /* Pre-EV6 CPUs interpreted this as HW_REI, loading the return
               address from EXC_ADDR.  This turns out to be useful for our
               emulation PALcode, so continue to accept it.  */
            vb = dest_sink(ctx);
            tcg_gen_ld_i64(vb, tcg_env, offsetof(CPUAlphaState, exc_addr));
        } else {
            vb = load_gpr(ctx, rb);
        }
        tcg_gen_movi_i64(cpu_lock_addr, -1);
        st_flag_byte(load_zero(ctx), ENV_FLAG_RX_SHIFT);
        tmp = tcg_temp_new();
        tcg_gen_andi_i64(tmp, vb, 1);
        st_flag_byte(tmp, ENV_FLAG_PAL_SHIFT);
        tcg_gen_andi_i64(cpu_pc, vb, ~3);
        /* Allow interrupts to be recognized right away.  */
        ret = DISAS_PC_UPDATED_NOCHAIN;
        break;
#else
        goto invalid_opc;
#endif

    case 0x1F:
        /* HW_ST (PALcode) */
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        {
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access */
                va = load_gpr(ctx, ra);
                vb = load_gpr(ctx, rb);
                tmp = tcg_temp_new();
                tcg_gen_addi_i64(tmp, vb, disp12);
                tcg_gen_qemu_st_i64(va, tmp, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x1:
                /* Quadword physical access */
                va = load_gpr(ctx, ra);
                vb = load_gpr(ctx, rb);
                tmp = tcg_temp_new();
                tcg_gen_addi_i64(tmp, vb, disp12);
                tcg_gen_qemu_st_i64(va, tmp, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x2:
                /* Longword physical access with lock */
                ret = gen_store_conditional(ctx, ra, rb, disp12,
                                            MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x3:
                /* Quadword physical access with lock */
                ret = gen_store_conditional(ctx, ra, rb, disp12,
                                            MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x4:
                /* Longword virtual access */
                goto invalid_opc;
            case 0x5:
                /* Quadword virtual access */
                goto invalid_opc;
            case 0x6:
                /* Invalid */
                goto invalid_opc;
            case 0x7:
                /* Invalid */
                goto invalid_opc;
            case 0x8:
                /* Invalid */
                goto invalid_opc;
            case 0x9:
                /* Invalid */
                goto invalid_opc;
            case 0xA:
                /* Invalid */
                goto invalid_opc;
            case 0xB:
                /* Invalid */
                goto invalid_opc;
            case 0xC:
                /* Longword virtual access with alternate access mode */
                goto invalid_opc;
            case 0xD:
                /* Quadword virtual access with alternate access mode */
                goto invalid_opc;
            case 0xE:
                /* Invalid */
                goto invalid_opc;
            case 0xF:
                /* Invalid */
                goto invalid_opc;
            }
            break;
        }
#else
        goto invalid_opc;
#endif
    case 0x20:
        /* LDF */
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldf);
        break;
    case 0x21:
        /* LDG */
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldg);
        break;
    case 0x22:
        /* LDS */
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_lds);
        break;
    case 0x23:
        /* LDT */
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldt);
        break;
    case 0x24:
        /* STF */
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stf);
        break;
    case 0x25:
        /* STG */
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stg);
        break;
    case 0x26:
        /* STS */
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_sts);
        break;
    case 0x27:
        /* STT */
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stt);
        break;
    case 0x28:
        /* LDL */
        gen_load_int(ctx, ra, rb, disp16, MO_LESL, 0, 0);
        break;
    case 0x29:
        /* LDQ */
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ, 0, 0);
        break;
    case 0x2A:
        /* LDL_L */
        gen_load_int(ctx, ra, rb, disp16, MO_LESL | MO_ALIGN, 0, 1);
        break;
    case 0x2B:
        /* LDQ_L */
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ | MO_ALIGN, 0, 1);
        break;
    case 0x2C:
        /* STL */
        gen_store_int(ctx, ra, rb, disp16, MO_LEUL, 0);
        break;
    case 0x2D:
        /* STQ */
        gen_store_int(ctx, ra, rb, disp16, MO_LEUQ, 0);
        break;
    case 0x2E:
        /* STL_C */
        ret = gen_store_conditional(ctx, ra, rb, disp16,
                                    ctx->mem_idx, MO_LESL | MO_ALIGN);
        break;
    case 0x2F:
        /* STQ_C */
        ret = gen_store_conditional(ctx, ra, rb, disp16,
                                    ctx->mem_idx, MO_LEUQ | MO_ALIGN);
        break;
    case 0x30:
        /* BR */
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x31: /* FBEQ */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_EQ, ra, disp21);
        break;
    case 0x32: /* FBLT */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_LT, ra, disp21);
        break;
    case 0x33: /* FBLE */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_LE, ra, disp21);
        break;
    case 0x34:
        /* BSR */
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x35: /* FBNE */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_NE, ra, disp21);
        break;
    case 0x36: /* FBGE */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_GE, ra, disp21);
        break;
    case 0x37: /* FBGT */
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_GT, ra, disp21);
        break;
    case 0x38:
        /* BLBC */
        ret = gen_bcond(ctx, TCG_COND_EQ, ra, disp21, 1);
        break;
    case 0x39:
        /* BEQ */
        ret = gen_bcond(ctx, TCG_COND_EQ, ra, disp21, 0);
        break;
    case 0x3A:
        /* BLT */
        ret = gen_bcond(ctx, TCG_COND_LT, ra, disp21, 0);
        break;
    case 0x3B:
        /* BLE */
        ret = gen_bcond(ctx, TCG_COND_LE, ra, disp21, 0);
        break;
    case 0x3C:
        /* BLBS */
        ret = gen_bcond(ctx, TCG_COND_NE, ra, disp21, 1);
        break;
    case 0x3D:
        /* BNE */
        ret = gen_bcond(ctx, TCG_COND_NE, ra, disp21, 0);
        break;
    case 0x3E:
        /* BGE */
        ret = gen_bcond(ctx, TCG_COND_GE, ra, disp21, 0);
        break;
    case 0x3F:
        /* BGT */
        ret = gen_bcond(ctx, TCG_COND_GT, ra, disp21, 0);
        break;
    invalid_opc:
        ret = gen_invalid(ctx);
        break;
    raise_fen:
        ret = gen_excp(ctx, EXCP_FEN, 0);
        break;
    }

    return ret;
}

static void alpha_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUAlphaState *env = cpu_env(cpu);
    int64_t bound;

    ctx->tbflags = ctx->base.tb->flags;
    ctx->mem_idx = cpu_mmu_index(env, false);
    ctx->implver = env->implver;
    ctx->amask = env->amask;

#ifdef CONFIG_USER_ONLY
    ctx->ir = cpu_std_ir;
    ctx->unalign = (ctx->tbflags & TB_FLAG_UNALIGN ? MO_UNALN : MO_ALIGN);
#else
    ctx->palbr = env->palbr;
    ctx->ir = (ctx->tbflags & ENV_FLAG_PAL_MODE ? cpu_pal_ir : cpu_std_ir);
#endif

    /* ??? Every TB begins with unset rounding mode, to be initialized on
       the first fp insn of the TB.  Alternately we could define a proper
       default for every TB (e.g. QUAL_RM_N or QUAL_RM_D) and make sure
       to reset the FP_STATUS to that default at the end of any TB that
       changes the default.  We could even (gasp) dynamically figure out
       what default would be most efficient given the running program.  */
    ctx->tb_rm = -1;
    /* Similarly for flush-to-zero.  */
    ctx->tb_ftz = -1;

    ctx->zero = NULL;
    ctx->sink = NULL;

    /* Bound the number of insns to execute to those left on the page.  */
    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);
}

static void alpha_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void alpha_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    tcg_gen_insn_start(dcbase->pc_next);
}

static void alpha_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUAlphaState *env = cpu_env(cpu);
    uint32_t insn = translator_ldl(env, &ctx->base, ctx->base.pc_next);

    ctx->base.pc_next += 4;
    ctx->base.is_jmp = translate_one(ctx, insn);

    free_context_temps(ctx);
}

static void alpha_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        if (use_goto_tb(ctx, ctx->base.pc_next)) {
            tcg_gen_goto_tb(0);
            tcg_gen_movi_i64(cpu_pc, ctx->base.pc_next);
            tcg_gen_exit_tb(ctx->base.tb, 0);
        }
        /* FALLTHRU */
    case DISAS_PC_STALE:
        tcg_gen_movi_i64(cpu_pc, ctx->base.pc_next);
        /* FALLTHRU */
    case DISAS_PC_UPDATED:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_PC_UPDATED_NOCHAIN:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void alpha_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps alpha_tr_ops = {
    .init_disas_context = alpha_tr_init_disas_context,
    .tb_start           = alpha_tr_tb_start,
    .insn_start         = alpha_tr_insn_start,
    .translate_insn     = alpha_tr_translate_insn,
    .tb_stop            = alpha_tr_tb_stop,
    .disas_log          = alpha_tr_disas_log,
};

void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc;
    translator_loop(cpu, tb, max_insns, pc, host_pc, &alpha_tr_ops, &dc.base);
}
