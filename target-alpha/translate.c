/*
 *  Alpha emulation cpu translation for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "host-utils.h"
#include "tcg-op.h"
#include "qemu-common.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

#undef ALPHA_DEBUG_DISAS
#define CONFIG_SOFTFLOAT_INLINE

#ifdef ALPHA_DEBUG_DISAS
#  define LOG_DISAS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

typedef struct DisasContext DisasContext;
struct DisasContext {
    struct TranslationBlock *tb;
    CPUAlphaState *env;
    uint64_t pc;
    int mem_idx;

    /* Current rounding mode for this TB.  */
    int tb_rm;
    /* Current flush-to-zero setting for this TB.  */
    int tb_ftz;
};

/* Return values from translate_one, indicating the state of the TB.
   Note that zero indicates that we are not exiting the TB.  */

typedef enum {
    NO_EXIT,

    /* We have emitted one or more goto_tb.  No fixup required.  */
    EXIT_GOTO_TB,

    /* We are not using a goto_tb (for whatever reason), but have updated
       the PC (for whatever reason), so there's no need to do it again on
       exiting the TB.  */
    EXIT_PC_UPDATED,

    /* We are exiting the TB, but have neither emitted a goto_tb, nor
       updated the PC for the next instruction to be executed.  */
    EXIT_PC_STALE,

    /* We are ending the TB with a noreturn function call, e.g. longjmp.
       No following code will be executed.  */
    EXIT_NORETURN,
} ExitStatus;

/* global register indexes */
static TCGv_ptr cpu_env;
static TCGv cpu_ir[31];
static TCGv cpu_fir[31];
static TCGv cpu_pc;
static TCGv cpu_lock_addr;
static TCGv cpu_lock_st_addr;
static TCGv cpu_lock_value;
static TCGv cpu_unique;
#ifndef CONFIG_USER_ONLY
static TCGv cpu_sysval;
static TCGv cpu_usp;
#endif

/* register names */
static char cpu_reg_names[10*4+21*5 + 10*5+21*6];

#include "gen-icount.h"

static void alpha_translate_init(void)
{
    int i;
    char *p;
    static int done_init = 0;

    if (done_init)
        return;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    p = cpu_reg_names;
    for (i = 0; i < 31; i++) {
        sprintf(p, "ir%d", i);
        cpu_ir[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                           offsetof(CPUState, ir[i]), p);
        p += (i < 10) ? 4 : 5;

        sprintf(p, "fir%d", i);
        cpu_fir[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                            offsetof(CPUState, fir[i]), p);
        p += (i < 10) ? 5 : 6;
    }

    cpu_pc = tcg_global_mem_new_i64(TCG_AREG0,
                                    offsetof(CPUState, pc), "pc");

    cpu_lock_addr = tcg_global_mem_new_i64(TCG_AREG0,
					   offsetof(CPUState, lock_addr),
					   "lock_addr");
    cpu_lock_st_addr = tcg_global_mem_new_i64(TCG_AREG0,
					      offsetof(CPUState, lock_st_addr),
					      "lock_st_addr");
    cpu_lock_value = tcg_global_mem_new_i64(TCG_AREG0,
					    offsetof(CPUState, lock_value),
					    "lock_value");

    cpu_unique = tcg_global_mem_new_i64(TCG_AREG0,
                                        offsetof(CPUState, unique), "unique");
#ifndef CONFIG_USER_ONLY
    cpu_sysval = tcg_global_mem_new_i64(TCG_AREG0,
                                        offsetof(CPUState, sysval), "sysval");
    cpu_usp = tcg_global_mem_new_i64(TCG_AREG0,
                                     offsetof(CPUState, usp), "usp");
#endif

    /* register helpers */
#define GEN_HELPER 2
#include "helper.h"

    done_init = 1;
}

static void gen_excp_1(int exception, int error_code)
{
    TCGv_i32 tmp1, tmp2;

    tmp1 = tcg_const_i32(exception);
    tmp2 = tcg_const_i32(error_code);
    gen_helper_excp(tmp1, tmp2);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_i32(tmp1);
}

static ExitStatus gen_excp(DisasContext *ctx, int exception, int error_code)
{
    tcg_gen_movi_i64(cpu_pc, ctx->pc);
    gen_excp_1(exception, error_code);
    return EXIT_NORETURN;
}

static inline ExitStatus gen_invalid(DisasContext *ctx)
{
    return gen_excp(ctx, EXCP_OPCDEC, 0);
}

static inline void gen_qemu_ldf(TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld32u(tmp, t1, flags);
    tcg_gen_trunc_i64_i32(tmp32, tmp);
    gen_helper_memory_to_f(t0, tmp32);
    tcg_temp_free_i32(tmp32);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_ldg(TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_qemu_ld64(tmp, t1, flags);
    gen_helper_memory_to_g(t0, tmp);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_lds(TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld32u(tmp, t1, flags);
    tcg_gen_trunc_i64_i32(tmp32, tmp);
    gen_helper_memory_to_s(t0, tmp32);
    tcg_temp_free_i32(tmp32);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_ldl_l(TCGv t0, TCGv t1, int flags)
{
    tcg_gen_qemu_ld32s(t0, t1, flags);
    tcg_gen_mov_i64(cpu_lock_addr, t1);
    tcg_gen_mov_i64(cpu_lock_value, t0);
}

static inline void gen_qemu_ldq_l(TCGv t0, TCGv t1, int flags)
{
    tcg_gen_qemu_ld64(t0, t1, flags);
    tcg_gen_mov_i64(cpu_lock_addr, t1);
    tcg_gen_mov_i64(cpu_lock_value, t0);
}

static inline void gen_load_mem(DisasContext *ctx,
                                void (*tcg_gen_qemu_load)(TCGv t0, TCGv t1,
                                                          int flags),
                                int ra, int rb, int32_t disp16, int fp,
                                int clear)
{
    TCGv addr, va;

    /* LDQ_U with ra $31 is UNOP.  Other various loads are forms of
       prefetches, which we can treat as nops.  No worries about
       missed exceptions here.  */
    if (unlikely(ra == 31)) {
        return;
    }

    addr = tcg_temp_new();
    if (rb != 31) {
        tcg_gen_addi_i64(addr, cpu_ir[rb], disp16);
        if (clear) {
            tcg_gen_andi_i64(addr, addr, ~0x7);
        }
    } else {
        if (clear) {
            disp16 &= ~0x7;
        }
        tcg_gen_movi_i64(addr, disp16);
    }

    va = (fp ? cpu_fir[ra] : cpu_ir[ra]);
    tcg_gen_qemu_load(va, addr, ctx->mem_idx);

    tcg_temp_free(addr);
}

static inline void gen_qemu_stf(TCGv t0, TCGv t1, int flags)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    TCGv tmp = tcg_temp_new();
    gen_helper_f_to_memory(tmp32, t0);
    tcg_gen_extu_i32_i64(tmp, tmp32);
    tcg_gen_qemu_st32(tmp, t1, flags);
    tcg_temp_free(tmp);
    tcg_temp_free_i32(tmp32);
}

static inline void gen_qemu_stg(TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    gen_helper_g_to_memory(tmp, t0);
    tcg_gen_qemu_st64(tmp, t1, flags);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_sts(TCGv t0, TCGv t1, int flags)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    TCGv tmp = tcg_temp_new();
    gen_helper_s_to_memory(tmp32, t0);
    tcg_gen_extu_i32_i64(tmp, tmp32);
    tcg_gen_qemu_st32(tmp, t1, flags);
    tcg_temp_free(tmp);
    tcg_temp_free_i32(tmp32);
}

static inline void gen_store_mem(DisasContext *ctx,
                                 void (*tcg_gen_qemu_store)(TCGv t0, TCGv t1,
                                                            int flags),
                                 int ra, int rb, int32_t disp16, int fp,
                                 int clear)
{
    TCGv addr, va;

    addr = tcg_temp_new();
    if (rb != 31) {
        tcg_gen_addi_i64(addr, cpu_ir[rb], disp16);
        if (clear) {
            tcg_gen_andi_i64(addr, addr, ~0x7);
        }
    } else {
        if (clear) {
            disp16 &= ~0x7;
        }
        tcg_gen_movi_i64(addr, disp16);
    }

    if (ra == 31) {
        va = tcg_const_i64(0);
    } else {
        va = (fp ? cpu_fir[ra] : cpu_ir[ra]);
    }
    tcg_gen_qemu_store(va, addr, ctx->mem_idx);

    tcg_temp_free(addr);
    if (ra == 31) {
        tcg_temp_free(va);
    }
}

static ExitStatus gen_store_conditional(DisasContext *ctx, int ra, int rb,
                                        int32_t disp16, int quad)
{
    TCGv addr;

    if (ra == 31) {
        /* ??? Don't bother storing anything.  The user can't tell
           the difference, since the zero register always reads zero.  */
        return NO_EXIT;
    }

#if defined(CONFIG_USER_ONLY)
    addr = cpu_lock_st_addr;
#else
    addr = tcg_temp_local_new();
#endif

    if (rb != 31) {
        tcg_gen_addi_i64(addr, cpu_ir[rb], disp16);
    } else {
        tcg_gen_movi_i64(addr, disp16);
    }

#if defined(CONFIG_USER_ONLY)
    /* ??? This is handled via a complicated version of compare-and-swap
       in the cpu_loop.  Hopefully one day we'll have a real CAS opcode
       in TCG so that this isn't necessary.  */
    return gen_excp(ctx, quad ? EXCP_STQ_C : EXCP_STL_C, ra);
#else
    /* ??? In system mode we are never multi-threaded, so CAS can be
       implemented via a non-atomic load-compare-store sequence.  */
    {
        int lab_fail, lab_done;
        TCGv val;

        lab_fail = gen_new_label();
        lab_done = gen_new_label();
        tcg_gen_brcond_i64(TCG_COND_NE, addr, cpu_lock_addr, lab_fail);

        val = tcg_temp_new();
        if (quad) {
            tcg_gen_qemu_ld64(val, addr, ctx->mem_idx);
        } else {
            tcg_gen_qemu_ld32s(val, addr, ctx->mem_idx);
        }
        tcg_gen_brcond_i64(TCG_COND_NE, val, cpu_lock_value, lab_fail);

        if (quad) {
            tcg_gen_qemu_st64(cpu_ir[ra], addr, ctx->mem_idx);
        } else {
            tcg_gen_qemu_st32(cpu_ir[ra], addr, ctx->mem_idx);
        }
        tcg_gen_movi_i64(cpu_ir[ra], 1);
        tcg_gen_br(lab_done);

        gen_set_label(lab_fail);
        tcg_gen_movi_i64(cpu_ir[ra], 0);

        gen_set_label(lab_done);
        tcg_gen_movi_i64(cpu_lock_addr, -1);

        tcg_temp_free(addr);
        return NO_EXIT;
    }
#endif
}

static int use_goto_tb(DisasContext *ctx, uint64_t dest)
{
    /* Check for the dest on the same page as the start of the TB.  We
       also want to suppress goto_tb in the case of single-steping and IO.  */
    return (((ctx->tb->pc ^ dest) & TARGET_PAGE_MASK) == 0
            && !ctx->env->singlestep_enabled
            && !(ctx->tb->cflags & CF_LAST_IO));
}

static ExitStatus gen_bdirect(DisasContext *ctx, int ra, int32_t disp)
{
    uint64_t dest = ctx->pc + (disp << 2);

    if (ra != 31) {
        tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
    }

    /* Notice branch-to-next; used to initialize RA with the PC.  */
    if (disp == 0) {
        return 0;
    } else if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(cpu_pc, dest);
        tcg_gen_exit_tb((tcg_target_long)ctx->tb);
        return EXIT_GOTO_TB;
    } else {
        tcg_gen_movi_i64(cpu_pc, dest);
        return EXIT_PC_UPDATED;
    }
}

static ExitStatus gen_bcond_internal(DisasContext *ctx, TCGCond cond,
                                     TCGv cmp, int32_t disp)
{
    uint64_t dest = ctx->pc + (disp << 2);
    int lab_true = gen_new_label();

    if (use_goto_tb(ctx, dest)) {
        tcg_gen_brcondi_i64(cond, cmp, 0, lab_true);

        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(cpu_pc, ctx->pc);
        tcg_gen_exit_tb((tcg_target_long)ctx->tb);

        gen_set_label(lab_true);
        tcg_gen_goto_tb(1);
        tcg_gen_movi_i64(cpu_pc, dest);
        tcg_gen_exit_tb((tcg_target_long)ctx->tb + 1);

        return EXIT_GOTO_TB;
    } else {
        int lab_over = gen_new_label();

        /* ??? Consider using either
             movi pc, next
             addi tmp, pc, disp
             movcond pc, cond, 0, tmp, pc
           or
             setcond tmp, cond, 0
             movi pc, next
             neg tmp, tmp
             andi tmp, tmp, disp
             add pc, pc, tmp
           The current diamond subgraph surely isn't efficient.  */

        tcg_gen_brcondi_i64(cond, cmp, 0, lab_true);
        tcg_gen_movi_i64(cpu_pc, ctx->pc);
        tcg_gen_br(lab_over);
        gen_set_label(lab_true);
        tcg_gen_movi_i64(cpu_pc, dest);
        gen_set_label(lab_over);

        return EXIT_PC_UPDATED;
    }
}

static ExitStatus gen_bcond(DisasContext *ctx, TCGCond cond, int ra,
                            int32_t disp, int mask)
{
    TCGv cmp_tmp;

    if (unlikely(ra == 31)) {
        cmp_tmp = tcg_const_i64(0);
    } else {
        cmp_tmp = tcg_temp_new();
        if (mask) {
            tcg_gen_andi_i64(cmp_tmp, cpu_ir[ra], 1);
        } else {
            tcg_gen_mov_i64(cmp_tmp, cpu_ir[ra]);
        }
    }

    return gen_bcond_internal(ctx, cond, cmp_tmp, disp);
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
        /* For >= or <, map -0.0 to +0.0 via comparison and mask.  */
        tcg_gen_setcondi_i64(TCG_COND_NE, dest, src, mzero);
        tcg_gen_neg_i64(dest, dest);
        tcg_gen_and_i64(dest, dest, src);
        break;

    default:
        abort();
    }
}

static ExitStatus gen_fbcond(DisasContext *ctx, TCGCond cond, int ra,
                             int32_t disp)
{
    TCGv cmp_tmp;

    if (unlikely(ra == 31)) {
        /* Very uncommon case, but easier to optimize it to an integer
           comparison than continuing with the floating point comparison.  */
        return gen_bcond(ctx, cond, ra, disp, 0);
    }

    cmp_tmp = tcg_temp_new();
    gen_fold_mzero(cond, cmp_tmp, cpu_fir[ra]);
    return gen_bcond_internal(ctx, cond, cmp_tmp, disp);
}

static void gen_cmov(TCGCond cond, int ra, int rb, int rc,
                     int islit, uint8_t lit, int mask)
{
    TCGCond inv_cond = tcg_invert_cond(cond);
    int l1;

    if (unlikely(rc == 31))
        return;

    l1 = gen_new_label();

    if (ra != 31) {
        if (mask) {
            TCGv tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[ra], 1);
            tcg_gen_brcondi_i64(inv_cond, tmp, 0, l1);
            tcg_temp_free(tmp);
        } else
            tcg_gen_brcondi_i64(inv_cond, cpu_ir[ra], 0, l1);
    } else {
        /* Very uncommon case - Do not bother to optimize.  */
        TCGv tmp = tcg_const_i64(0);
        tcg_gen_brcondi_i64(inv_cond, tmp, 0, l1);
        tcg_temp_free(tmp);
    }

    if (islit)
        tcg_gen_movi_i64(cpu_ir[rc], lit);
    else
        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
    gen_set_label(l1);
}

static void gen_fcmov(TCGCond cond, int ra, int rb, int rc)
{
    TCGv cmp_tmp;
    int l1;

    if (unlikely(rc == 31)) {
        return;
    }

    cmp_tmp = tcg_temp_new();
    if (unlikely(ra == 31)) {
        tcg_gen_movi_i64(cmp_tmp, 0);
    } else {
        gen_fold_mzero(cond, cmp_tmp, cpu_fir[ra]);
    }

    l1 = gen_new_label();
    tcg_gen_brcondi_i64(tcg_invert_cond(cond), cmp_tmp, 0, l1);
    tcg_temp_free(cmp_tmp);

    if (rb != 31)
        tcg_gen_mov_i64(cpu_fir[rc], cpu_fir[rb]);
    else
        tcg_gen_movi_i64(cpu_fir[rc], 0);
    gen_set_label(l1);
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
        tcg_gen_ld8u_i32(tmp, cpu_env, offsetof(CPUState, fpcr_dyn_round));
        break;
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    /* ??? The "softfloat.h" interface is to call set_float_rounding_mode.
       With CONFIG_SOFTFLOAT that expands to an out-of-line call that just
       sets the one field.  */
    tcg_gen_st8_i32(tmp, cpu_env,
                    offsetof(CPUState, fp_status.float_rounding_mode));
#else
    gen_helper_setroundmode(tmp);
#endif

    tcg_temp_free_i32(tmp);
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
        tcg_gen_ld8u_i32(tmp, cpu_env, offsetof(CPUState, fpcr_flush_to_zero));
    } else {
        /* Underflow is disabled, force flush-to-zero.  */
        tcg_gen_movi_i32(tmp, 1);
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    tcg_gen_st8_i32(tmp, cpu_env,
                    offsetof(CPUState, fp_status.flush_to_zero));
#else
    gen_helper_setflushzero(tmp);
#endif

    tcg_temp_free_i32(tmp);
}

static TCGv gen_ieee_input(int reg, int fn11, int is_cmp)
{
    TCGv val = tcg_temp_new();
    if (reg == 31) {
        tcg_gen_movi_i64(val, 0);
    } else if (fn11 & QUAL_S) {
        gen_helper_ieee_input_s(val, cpu_fir[reg]);
    } else if (is_cmp) {
        gen_helper_ieee_input_cmp(val, cpu_fir[reg]);
    } else {
        gen_helper_ieee_input(val, cpu_fir[reg]);
    }
    return val;
}

static void gen_fp_exc_clear(void)
{
#if defined(CONFIG_SOFTFLOAT_INLINE)
    TCGv_i32 zero = tcg_const_i32(0);
    tcg_gen_st8_i32(zero, cpu_env,
                    offsetof(CPUState, fp_status.float_exception_flags));
    tcg_temp_free_i32(zero);
#else
    gen_helper_fp_exc_clear();
#endif
}

static void gen_fp_exc_raise_ignore(int rc, int fn11, int ignore)
{
    /* ??? We ought to be able to do something with imprecise exceptions.
       E.g. notice we're still in the trap shadow of something within the
       TB and do not generate the code to signal the exception; end the TB
       when an exception is forced to arrive, either by consumption of a
       register value or TRAPB or EXCB.  */
    TCGv_i32 exc = tcg_temp_new_i32();
    TCGv_i32 reg;

#if defined(CONFIG_SOFTFLOAT_INLINE)
    tcg_gen_ld8u_i32(exc, cpu_env,
                     offsetof(CPUState, fp_status.float_exception_flags));
#else
    gen_helper_fp_exc_get(exc);
#endif

    if (ignore) {
        tcg_gen_andi_i32(exc, exc, ~ignore);
    }

    /* ??? Pass in the regno of the destination so that the helper can
       set EXC_MASK, which contains a bitmask of destination registers
       that have caused arithmetic traps.  A simple userspace emulation
       does not require this.  We do need it for a guest kernel's entArith,
       or if we were to do something clever with imprecise exceptions.  */
    reg = tcg_const_i32(rc + 32);

    if (fn11 & QUAL_S) {
        gen_helper_fp_exc_raise_s(exc, reg);
    } else {
        gen_helper_fp_exc_raise(exc, reg);
    }

    tcg_temp_free_i32(reg);
    tcg_temp_free_i32(exc);
}

static inline void gen_fp_exc_raise(int rc, int fn11)
{
    gen_fp_exc_raise_ignore(rc, fn11, fn11 & QUAL_I ? 0 : float_flag_inexact);
}

static void gen_fcvtlq(int rb, int rc)
{
    if (unlikely(rc == 31)) {
        return;
    }
    if (unlikely(rb == 31)) {
        tcg_gen_movi_i64(cpu_fir[rc], 0);
    } else {
        TCGv tmp = tcg_temp_new();

        /* The arithmetic right shift here, plus the sign-extended mask below
           yields a sign-extended result without an explicit ext32s_i64.  */
        tcg_gen_sari_i64(tmp, cpu_fir[rb], 32);
        tcg_gen_shri_i64(cpu_fir[rc], cpu_fir[rb], 29);
        tcg_gen_andi_i64(tmp, tmp, (int32_t)0xc0000000);
        tcg_gen_andi_i64(cpu_fir[rc], cpu_fir[rc], 0x3fffffff);
        tcg_gen_or_i64(cpu_fir[rc], cpu_fir[rc], tmp);

        tcg_temp_free(tmp);
    }
}

static void gen_fcvtql(int rb, int rc)
{
    if (unlikely(rc == 31)) {
        return;
    }
    if (unlikely(rb == 31)) {
        tcg_gen_movi_i64(cpu_fir[rc], 0);
    } else {
        TCGv tmp = tcg_temp_new();

        tcg_gen_andi_i64(tmp, cpu_fir[rb], 0xC0000000);
        tcg_gen_andi_i64(cpu_fir[rc], cpu_fir[rb], 0x3FFFFFFF);
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_shli_i64(cpu_fir[rc], cpu_fir[rc], 29);
        tcg_gen_or_i64(cpu_fir[rc], cpu_fir[rc], tmp);

        tcg_temp_free(tmp);
    }
}

static void gen_fcvtql_v(DisasContext *ctx, int rb, int rc)
{
    if (rb != 31) {
        int lab = gen_new_label();
        TCGv tmp = tcg_temp_new();

        tcg_gen_ext32s_i64(tmp, cpu_fir[rb]);
        tcg_gen_brcond_i64(TCG_COND_EQ, tmp, cpu_fir[rb], lab);
        gen_excp(ctx, EXCP_ARITH, EXC_M_IOV);

        gen_set_label(lab);
    }
    gen_fcvtql(rb, rc);
}

#define FARITH2(name)                                   \
static inline void glue(gen_f, name)(int rb, int rc)    \
{                                                       \
    if (unlikely(rc == 31)) {                           \
        return;                                         \
    }                                                   \
    if (rb != 31) {                                     \
        gen_helper_ ## name (cpu_fir[rc], cpu_fir[rb]); \
    } else {						\
        TCGv tmp = tcg_const_i64(0);                    \
        gen_helper_ ## name (cpu_fir[rc], tmp);         \
        tcg_temp_free(tmp);                             \
    }                                                   \
}

/* ??? VAX instruction qualifiers ignored.  */
FARITH2(sqrtf)
FARITH2(sqrtg)
FARITH2(cvtgf)
FARITH2(cvtgq)
FARITH2(cvtqf)
FARITH2(cvtqg)

static void gen_ieee_arith2(DisasContext *ctx, void (*helper)(TCGv, TCGv),
                            int rb, int rc, int fn11)
{
    TCGv vb;

    /* ??? This is wrong: the instruction is not a nop, it still may
       raise exceptions.  */
    if (unlikely(rc == 31)) {
        return;
    }

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);
    gen_fp_exc_clear();

    vb = gen_ieee_input(rb, fn11, 0);
    helper(cpu_fir[rc], vb);
    tcg_temp_free(vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH2(name)                                       \
static inline void glue(gen_f, name)(DisasContext *ctx,         \
                                     int rb, int rc, int fn11)  \
{                                                               \
    gen_ieee_arith2(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_ARITH2(sqrts)
IEEE_ARITH2(sqrtt)
IEEE_ARITH2(cvtst)
IEEE_ARITH2(cvtts)

static void gen_fcvttq(DisasContext *ctx, int rb, int rc, int fn11)
{
    TCGv vb;
    int ignore = 0;

    /* ??? This is wrong: the instruction is not a nop, it still may
       raise exceptions.  */
    if (unlikely(rc == 31)) {
        return;
    }

    /* No need to set flushzero, since we have an integer output.  */
    gen_fp_exc_clear();
    vb = gen_ieee_input(rb, fn11, 0);

    /* Almost all integer conversions use cropped rounding, and most
       also do not have integer overflow enabled.  Special case that.  */
    switch (fn11) {
    case QUAL_RM_C:
        gen_helper_cvttq_c(cpu_fir[rc], vb);
        break;
    case QUAL_V | QUAL_RM_C:
    case QUAL_S | QUAL_V | QUAL_RM_C:
        ignore = float_flag_inexact;
        /* FALLTHRU */
    case QUAL_S | QUAL_V | QUAL_I | QUAL_RM_C:
        gen_helper_cvttq_svic(cpu_fir[rc], vb);
        break;
    default:
        gen_qual_roundmode(ctx, fn11);
        gen_helper_cvttq(cpu_fir[rc], vb);
        ignore |= (fn11 & QUAL_V ? 0 : float_flag_overflow);
        ignore |= (fn11 & QUAL_I ? 0 : float_flag_inexact);
        break;
    }
    tcg_temp_free(vb);

    gen_fp_exc_raise_ignore(rc, fn11, ignore);
}

static void gen_ieee_intcvt(DisasContext *ctx, void (*helper)(TCGv, TCGv),
			    int rb, int rc, int fn11)
{
    TCGv vb;

    /* ??? This is wrong: the instruction is not a nop, it still may
       raise exceptions.  */
    if (unlikely(rc == 31)) {
        return;
    }

    gen_qual_roundmode(ctx, fn11);

    if (rb == 31) {
        vb = tcg_const_i64(0);
    } else {
        vb = cpu_fir[rb];
    }

    /* The only exception that can be raised by integer conversion
       is inexact.  Thus we only need to worry about exceptions when
       inexact handling is requested.  */
    if (fn11 & QUAL_I) {
        gen_fp_exc_clear();
        helper(cpu_fir[rc], vb);
        gen_fp_exc_raise(rc, fn11);
    } else {
        helper(cpu_fir[rc], vb);
    }

    if (rb == 31) {
        tcg_temp_free(vb);
    }
}

#define IEEE_INTCVT(name)                                       \
static inline void glue(gen_f, name)(DisasContext *ctx,         \
                                     int rb, int rc, int fn11)  \
{                                                               \
    gen_ieee_intcvt(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_INTCVT(cvtqs)
IEEE_INTCVT(cvtqt)

static void gen_cpys_internal(int ra, int rb, int rc, int inv_a, uint64_t mask)
{
    TCGv va, vb, vmask;
    int za = 0, zb = 0;

    if (unlikely(rc == 31)) {
        return;
    }

    vmask = tcg_const_i64(mask);

    TCGV_UNUSED_I64(va);
    if (ra == 31) {
        if (inv_a) {
            va = vmask;
        } else {
            za = 1;
        }
    } else {
        va = tcg_temp_new_i64();
        tcg_gen_mov_i64(va, cpu_fir[ra]);
        if (inv_a) {
            tcg_gen_andc_i64(va, vmask, va);
        } else {
            tcg_gen_and_i64(va, va, vmask);
        }
    }

    TCGV_UNUSED_I64(vb);
    if (rb == 31) {
        zb = 1;
    } else {
        vb = tcg_temp_new_i64();
        tcg_gen_andc_i64(vb, cpu_fir[rb], vmask);
    }

    switch (za << 1 | zb) {
    case 0 | 0:
        tcg_gen_or_i64(cpu_fir[rc], va, vb);
        break;
    case 0 | 1:
        tcg_gen_mov_i64(cpu_fir[rc], va);
        break;
    case 2 | 0:
        tcg_gen_mov_i64(cpu_fir[rc], vb);
        break;
    case 2 | 1:
        tcg_gen_movi_i64(cpu_fir[rc], 0);
        break;
    }

    tcg_temp_free(vmask);
    if (ra != 31) {
        tcg_temp_free(va);
    }
    if (rb != 31) {
        tcg_temp_free(vb);
    }
}

static inline void gen_fcpys(int ra, int rb, int rc)
{
    gen_cpys_internal(ra, rb, rc, 0, 0x8000000000000000ULL);
}

static inline void gen_fcpysn(int ra, int rb, int rc)
{
    gen_cpys_internal(ra, rb, rc, 1, 0x8000000000000000ULL);
}

static inline void gen_fcpyse(int ra, int rb, int rc)
{
    gen_cpys_internal(ra, rb, rc, 0, 0xFFF0000000000000ULL);
}

#define FARITH3(name)                                           \
static inline void glue(gen_f, name)(int ra, int rb, int rc)    \
{                                                               \
    TCGv va, vb;                                                \
                                                                \
    if (unlikely(rc == 31)) {                                   \
        return;                                                 \
    }                                                           \
    if (ra == 31) {                                             \
        va = tcg_const_i64(0);                                  \
    } else {                                                    \
        va = cpu_fir[ra];                                       \
    }                                                           \
    if (rb == 31) {                                             \
        vb = tcg_const_i64(0);                                  \
    } else {                                                    \
        vb = cpu_fir[rb];                                       \
    }                                                           \
                                                                \
    gen_helper_ ## name (cpu_fir[rc], va, vb);                  \
                                                                \
    if (ra == 31) {                                             \
        tcg_temp_free(va);                                      \
    }                                                           \
    if (rb == 31) {                                             \
        tcg_temp_free(vb);                                      \
    }                                                           \
}

/* ??? VAX instruction qualifiers ignored.  */
FARITH3(addf)
FARITH3(subf)
FARITH3(mulf)
FARITH3(divf)
FARITH3(addg)
FARITH3(subg)
FARITH3(mulg)
FARITH3(divg)
FARITH3(cmpgeq)
FARITH3(cmpglt)
FARITH3(cmpgle)

static void gen_ieee_arith3(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv, TCGv),
                            int ra, int rb, int rc, int fn11)
{
    TCGv va, vb;

    /* ??? This is wrong: the instruction is not a nop, it still may
       raise exceptions.  */
    if (unlikely(rc == 31)) {
        return;
    }

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);
    gen_fp_exc_clear();

    va = gen_ieee_input(ra, fn11, 0);
    vb = gen_ieee_input(rb, fn11, 0);
    helper(cpu_fir[rc], va, vb);
    tcg_temp_free(va);
    tcg_temp_free(vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH3(name)                                               \
static inline void glue(gen_f, name)(DisasContext *ctx,                 \
                                     int ra, int rb, int rc, int fn11)  \
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
                             void (*helper)(TCGv, TCGv, TCGv),
                             int ra, int rb, int rc, int fn11)
{
    TCGv va, vb;

    /* ??? This is wrong: the instruction is not a nop, it still may
       raise exceptions.  */
    if (unlikely(rc == 31)) {
        return;
    }

    gen_fp_exc_clear();

    va = gen_ieee_input(ra, fn11, 1);
    vb = gen_ieee_input(rb, fn11, 1);
    helper(cpu_fir[rc], va, vb);
    tcg_temp_free(va);
    tcg_temp_free(vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_CMP3(name)                                                 \
static inline void glue(gen_f, name)(DisasContext *ctx,                 \
                                     int ra, int rb, int rc, int fn11)  \
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
        if ((lit >> i) & 1)
            mask |= 0xffull << (i * 8);
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
        tcg_gen_andi_i64 (dest, src, zapnot_mask (lit));
        break;
    }
}

static inline void gen_zapnot(int ra, int rb, int rc, int islit, uint8_t lit)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else if (islit)
        gen_zapnoti(cpu_ir[rc], cpu_ir[ra], lit);
    else
        gen_helper_zapnot (cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
}

static inline void gen_zap(int ra, int rb, int rc, int islit, uint8_t lit)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else if (islit)
        gen_zapnoti(cpu_ir[rc], cpu_ir[ra], ~lit);
    else
        gen_helper_zap (cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
}


/* EXTWH, EXTLH, EXTQH */
static void gen_ext_h(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else {
        if (islit) {
            lit = (64 - (lit & 7) * 8) & 0x3f;
            tcg_gen_shli_i64(cpu_ir[rc], cpu_ir[ra], lit);
        } else {
            TCGv tmp1 = tcg_temp_new();
            tcg_gen_andi_i64(tmp1, cpu_ir[rb], 7);
            tcg_gen_shli_i64(tmp1, tmp1, 3);
            tcg_gen_neg_i64(tmp1, tmp1);
            tcg_gen_andi_i64(tmp1, tmp1, 0x3f);
            tcg_gen_shl_i64(cpu_ir[rc], cpu_ir[ra], tmp1);
            tcg_temp_free(tmp1);
        }
        gen_zapnoti(cpu_ir[rc], cpu_ir[rc], byte_mask);
    }
}

/* EXTBL, EXTWL, EXTLL, EXTQL */
static void gen_ext_l(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else {
        if (islit) {
            tcg_gen_shri_i64(cpu_ir[rc], cpu_ir[ra], (lit & 7) * 8);
        } else {
            TCGv tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[rb], 7);
            tcg_gen_shli_i64(tmp, tmp, 3);
            tcg_gen_shr_i64(cpu_ir[rc], cpu_ir[ra], tmp);
            tcg_temp_free(tmp);
        }
        gen_zapnoti(cpu_ir[rc], cpu_ir[rc], byte_mask);
    }
}

/* INSWH, INSLH, INSQH */
static void gen_ins_h(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31) || (islit && (lit & 7) == 0))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else {
        TCGv tmp = tcg_temp_new();

        /* The instruction description has us left-shift the byte mask
           and extract bits <15:8> and apply that zap at the end.  This
           is equivalent to simply performing the zap first and shifting
           afterward.  */
        gen_zapnoti (tmp, cpu_ir[ra], byte_mask);

        if (islit) {
            /* Note that we have handled the lit==0 case above.  */
            tcg_gen_shri_i64 (cpu_ir[rc], tmp, 64 - (lit & 7) * 8);
        } else {
            TCGv shift = tcg_temp_new();

            /* If (B & 7) == 0, we need to shift by 64 and leave a zero.
               Do this portably by splitting the shift into two parts:
               shift_count-1 and 1.  Arrange for the -1 by using
               ones-complement instead of twos-complement in the negation:
               ~((B & 7) * 8) & 63.  */

            tcg_gen_andi_i64(shift, cpu_ir[rb], 7);
            tcg_gen_shli_i64(shift, shift, 3);
            tcg_gen_not_i64(shift, shift);
            tcg_gen_andi_i64(shift, shift, 0x3f);

            tcg_gen_shr_i64(cpu_ir[rc], tmp, shift);
            tcg_gen_shri_i64(cpu_ir[rc], cpu_ir[rc], 1);
            tcg_temp_free(shift);
        }
        tcg_temp_free(tmp);
    }
}

/* INSBL, INSWL, INSLL, INSQL */
static void gen_ins_l(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else {
        TCGv tmp = tcg_temp_new();

        /* The instruction description has us left-shift the byte mask
           the same number of byte slots as the data and apply the zap
           at the end.  This is equivalent to simply performing the zap
           first and shifting afterward.  */
        gen_zapnoti (tmp, cpu_ir[ra], byte_mask);

        if (islit) {
            tcg_gen_shli_i64(cpu_ir[rc], tmp, (lit & 7) * 8);
        } else {
            TCGv shift = tcg_temp_new();
            tcg_gen_andi_i64(shift, cpu_ir[rb], 7);
            tcg_gen_shli_i64(shift, shift, 3);
            tcg_gen_shl_i64(cpu_ir[rc], tmp, shift);
            tcg_temp_free(shift);
        }
        tcg_temp_free(tmp);
    }
}

/* MSKWH, MSKLH, MSKQH */
static void gen_msk_h(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else if (islit) {
        gen_zapnoti (cpu_ir[rc], cpu_ir[ra], ~((byte_mask << (lit & 7)) >> 8));
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
           equivalent to ~((B & 7) * 8) & 63.  */

        tcg_gen_andi_i64(shift, cpu_ir[rb], 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_not_i64(shift, shift);
        tcg_gen_andi_i64(shift, shift, 0x3f);
        tcg_gen_movi_i64(mask, zapnot_mask (byte_mask));
        tcg_gen_shr_i64(mask, mask, shift);
        tcg_gen_shri_i64(mask, mask, 1);

        tcg_gen_andc_i64(cpu_ir[rc], cpu_ir[ra], mask);

        tcg_temp_free(mask);
        tcg_temp_free(shift);
    }
}

/* MSKBL, MSKWL, MSKLL, MSKQL */
static void gen_msk_l(int ra, int rb, int rc, int islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (unlikely(rc == 31))
        return;
    else if (unlikely(ra == 31))
        tcg_gen_movi_i64(cpu_ir[rc], 0);
    else if (islit) {
        gen_zapnoti (cpu_ir[rc], cpu_ir[ra], ~(byte_mask << (lit & 7)));
    } else {
        TCGv shift = tcg_temp_new();
        TCGv mask = tcg_temp_new();

        tcg_gen_andi_i64(shift, cpu_ir[rb], 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_movi_i64(mask, zapnot_mask (byte_mask));
        tcg_gen_shl_i64(mask, mask, shift);

        tcg_gen_andc_i64(cpu_ir[rc], cpu_ir[ra], mask);

        tcg_temp_free(mask);
        tcg_temp_free(shift);
    }
}

/* Code to call arith3 helpers */
#define ARITH3(name)                                                  \
static inline void glue(gen_, name)(int ra, int rb, int rc, int islit,\
                                    uint8_t lit)                      \
{                                                                     \
    if (unlikely(rc == 31))                                           \
        return;                                                       \
                                                                      \
    if (ra != 31) {                                                   \
        if (islit) {                                                  \
            TCGv tmp = tcg_const_i64(lit);                            \
            gen_helper_ ## name(cpu_ir[rc], cpu_ir[ra], tmp);         \
            tcg_temp_free(tmp);                                       \
        } else                                                        \
            gen_helper_ ## name (cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]); \
    } else {                                                          \
        TCGv tmp1 = tcg_const_i64(0);                                 \
        if (islit) {                                                  \
            TCGv tmp2 = tcg_const_i64(lit);                           \
            gen_helper_ ## name (cpu_ir[rc], tmp1, tmp2);             \
            tcg_temp_free(tmp2);                                      \
        } else                                                        \
            gen_helper_ ## name (cpu_ir[rc], tmp1, cpu_ir[rb]);       \
        tcg_temp_free(tmp1);                                          \
    }                                                                 \
}
ARITH3(cmpbge)
ARITH3(addlv)
ARITH3(sublv)
ARITH3(addqv)
ARITH3(subqv)
ARITH3(umulh)
ARITH3(mullv)
ARITH3(mulqv)
ARITH3(minub8)
ARITH3(minsb8)
ARITH3(minuw4)
ARITH3(minsw4)
ARITH3(maxub8)
ARITH3(maxsb8)
ARITH3(maxuw4)
ARITH3(maxsw4)
ARITH3(perr)

#define MVIOP2(name)                                    \
static inline void glue(gen_, name)(int rb, int rc)     \
{                                                       \
    if (unlikely(rc == 31))                             \
        return;                                         \
    if (unlikely(rb == 31))                             \
        tcg_gen_movi_i64(cpu_ir[rc], 0);                \
    else                                                \
        gen_helper_ ## name (cpu_ir[rc], cpu_ir[rb]);   \
}
MVIOP2(pklb)
MVIOP2(pkwb)
MVIOP2(unpkbl)
MVIOP2(unpkbw)

static void gen_cmp(TCGCond cond, int ra, int rb, int rc,
                    int islit, uint8_t lit)
{
    TCGv va, vb;

    if (unlikely(rc == 31)) {
        return;
    }

    if (ra == 31) {
        va = tcg_const_i64(0);
    } else {
        va = cpu_ir[ra];
    }
    if (islit) {
        vb = tcg_const_i64(lit);
    } else {
        vb = cpu_ir[rb];
    }

    tcg_gen_setcond_i64(cond, cpu_ir[rc], va, vb);

    if (ra == 31) {
        tcg_temp_free(va);
    }
    if (islit) {
        tcg_temp_free(vb);
    }
}

static void gen_rx(int ra, int set)
{
    TCGv_i32 tmp;

    if (ra != 31) {
        tcg_gen_ld8u_i64(cpu_ir[ra], cpu_env, offsetof(CPUState, intr_flag));
    }

    tmp = tcg_const_i32(set);
    tcg_gen_st8_i32(tmp, cpu_env, offsetof(CPUState, intr_flag));
    tcg_temp_free_i32(tmp);
}

static ExitStatus gen_call_pal(DisasContext *ctx, int palcode)
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
            tcg_gen_mov_i64(cpu_ir[IR_V0], cpu_unique);
            break;
        case 0x9F:
            /* WRUNIQUE */
            tcg_gen_mov_i64(cpu_unique, cpu_ir[IR_A0]);
            break;
        default:
            return gen_excp(ctx, EXCP_CALL_PAL, palcode & 0xbf);
        }
        return NO_EXIT;
    }

#ifndef CONFIG_USER_ONLY
    /* Privileged PAL code */
    if (palcode < 0x40 && (ctx->tb->flags & TB_FLAGS_USER_MODE) == 0) {
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
            tcg_gen_st_i64(cpu_ir[IR_A0], cpu_env, offsetof(CPUState, vptptr));
            break;
        case 0x31:
            /* WRVAL */
            tcg_gen_mov_i64(cpu_sysval, cpu_ir[IR_A0]);
            break;
        case 0x32:
            /* RDVAL */
            tcg_gen_mov_i64(cpu_ir[IR_V0], cpu_sysval);
            break;

        case 0x35: {
            /* SWPIPL */
            TCGv tmp;

            /* Note that we already know we're in kernel mode, so we know
               that PS only contains the 3 IPL bits.  */
            tcg_gen_ld8u_i64(cpu_ir[IR_V0], cpu_env, offsetof(CPUState, ps));

            /* But make sure and store only the 3 IPL bits from the user.  */
            tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[IR_A0], PS_INT_MASK);
            tcg_gen_st8_i64(tmp, cpu_env, offsetof(CPUState, ps));
            tcg_temp_free(tmp);
            break;
        }

        case 0x36:
            /* RDPS */
            tcg_gen_ld8u_i64(cpu_ir[IR_V0], cpu_env, offsetof(CPUState, ps));
            break;
        case 0x38:
            /* WRUSP */
            tcg_gen_mov_i64(cpu_usp, cpu_ir[IR_A0]);
            break;
        case 0x3A:
            /* RDUSP */
            tcg_gen_mov_i64(cpu_ir[IR_V0], cpu_usp);
            break;
        case 0x3C:
            /* WHAMI */
            tcg_gen_ld32s_i64(cpu_ir[IR_V0], cpu_env,
                              offsetof(CPUState, cpu_index));
            break;

        default:
            return gen_excp(ctx, EXCP_CALL_PAL, palcode & 0x3f);
        }
        return NO_EXIT;
    }
#endif

    return gen_invalid(ctx);
}

#ifndef CONFIG_USER_ONLY

#define PR_BYTE         0x100000
#define PR_LONG         0x200000

static int cpu_pr_data(int pr)
{
    switch (pr) {
    case  0: return offsetof(CPUAlphaState, ps) | PR_BYTE;
    case  1: return offsetof(CPUAlphaState, fen) | PR_BYTE;
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

    case 32 ... 39:
        return offsetof(CPUAlphaState, shadow[pr - 32]);
    case 40 ... 63:
        return offsetof(CPUAlphaState, scratch[pr - 40]);
    }
    return 0;
}

static void gen_mfpr(int ra, int regno)
{
    int data = cpu_pr_data(regno);

    /* In our emulated PALcode, these processor registers have no
       side effects from reading.  */
    if (ra == 31) {
        return;
    }

    /* The basic registers are data only, and unknown registers
       are read-zero, write-ignore.  */
    if (data == 0) {
        tcg_gen_movi_i64(cpu_ir[ra], 0);
    } else if (data & PR_BYTE) {
        tcg_gen_ld8u_i64(cpu_ir[ra], cpu_env, data & ~PR_BYTE);
    } else if (data & PR_LONG) {
        tcg_gen_ld32s_i64(cpu_ir[ra], cpu_env, data & ~PR_LONG);
    } else {
        tcg_gen_ld_i64(cpu_ir[ra], cpu_env, data);
    }
}

static void gen_mtpr(int rb, int regno)
{
    TCGv tmp;

    if (rb == 31) {
        tmp = tcg_const_i64(0);
    } else {
        tmp = cpu_ir[rb];
    }

    /* These two register numbers perform a TLB cache flush.  Thankfully we
       can only do this inside PALmode, which means that the current basic
       block cannot be affected by the change in mappings.  */
    if (regno == 255) {
        /* TBIA */
        gen_helper_tbia();
    } else if (regno == 254) {
        /* TBIS */
        gen_helper_tbis(tmp);
    } else {
        /* The basic registers are data only, and unknown registers
           are read-zero, write-ignore.  */
        int data = cpu_pr_data(regno);
        if (data != 0) {
            if (data & PR_BYTE) {
                tcg_gen_st8_i64(tmp, cpu_env, data & ~PR_BYTE);
            } else if (data & PR_LONG) {
                tcg_gen_st32_i64(tmp, cpu_env, data & ~PR_LONG);
            } else {
                tcg_gen_st_i64(tmp, cpu_env, data);
            }
        }
    }

    if (rb == 31) {
        tcg_temp_free(tmp);
    }
}
#endif /* !USER_ONLY*/

static ExitStatus translate_one(DisasContext *ctx, uint32_t insn)
{
    uint32_t palcode;
    int32_t disp21, disp16, disp12;
    uint16_t fn11;
    uint8_t opc, ra, rb, rc, fpfn, fn7, islit, real_islit;
    uint8_t lit;
    ExitStatus ret;

    /* Decode all instruction fields */
    opc = insn >> 26;
    ra = (insn >> 21) & 0x1F;
    rb = (insn >> 16) & 0x1F;
    rc = insn & 0x1F;
    real_islit = islit = (insn >> 12) & 1;
    if (rb == 31 && !islit) {
        islit = 1;
        lit = 0;
    } else
        lit = (insn >> 13) & 0xFF;
    palcode = insn & 0x03FFFFFF;
    disp21 = ((int32_t)((insn & 0x001FFFFF) << 11)) >> 11;
    disp16 = (int16_t)(insn & 0x0000FFFF);
    disp12 = (int32_t)((insn & 0x00000FFF) << 20) >> 20;
    fn11 = (insn >> 5) & 0x000007FF;
    fpfn = fn11 & 0x3F;
    fn7 = (insn >> 5) & 0x0000007F;
    LOG_DISAS("opc %02x ra %2d rb %2d rc %2d disp16 %6d\n",
              opc, ra, rb, rc, disp16);

    ret = NO_EXIT;
    switch (opc) {
    case 0x00:
        /* CALL_PAL */
        ret = gen_call_pal(ctx, palcode);
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
    case 0x08:
        /* LDA */
        if (likely(ra != 31)) {
            if (rb != 31)
                tcg_gen_addi_i64(cpu_ir[ra], cpu_ir[rb], disp16);
            else
                tcg_gen_movi_i64(cpu_ir[ra], disp16);
        }
        break;
    case 0x09:
        /* LDAH */
        if (likely(ra != 31)) {
            if (rb != 31)
                tcg_gen_addi_i64(cpu_ir[ra], cpu_ir[rb], disp16 << 16);
            else
                tcg_gen_movi_i64(cpu_ir[ra], disp16 << 16);
        }
        break;
    case 0x0A:
        /* LDBU */
        if (ctx->tb->flags & TB_FLAGS_AMASK_BWX) {
            gen_load_mem(ctx, &tcg_gen_qemu_ld8u, ra, rb, disp16, 0, 0);
            break;
        }
        goto invalid_opc;
    case 0x0B:
        /* LDQ_U */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 0, 1);
        break;
    case 0x0C:
        /* LDWU */
        if (ctx->tb->flags & TB_FLAGS_AMASK_BWX) {
            gen_load_mem(ctx, &tcg_gen_qemu_ld16u, ra, rb, disp16, 0, 0);
            break;
        }
        goto invalid_opc;
    case 0x0D:
        /* STW */
        gen_store_mem(ctx, &tcg_gen_qemu_st16, ra, rb, disp16, 0, 0);
        break;
    case 0x0E:
        /* STB */
        gen_store_mem(ctx, &tcg_gen_qemu_st8, ra, rb, disp16, 0, 0);
        break;
    case 0x0F:
        /* STQ_U */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 0, 1);
        break;
    case 0x10:
        switch (fn7) {
        case 0x00:
            /* ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit) {
                        tcg_gen_addi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    } else {
                        tcg_gen_add_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x02:
            /* S4ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_addi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_add_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x09:
            /* SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else {
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                }
            }
            break;
        case 0x0B:
            /* S4SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_subi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_sub_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else {
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                }
            }
            break;
        case 0x0F:
            /* CMPBGE */
            gen_cmpbge(ra, rb, rc, islit, lit);
            break;
        case 0x12:
            /* S8ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_addi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_add_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x1B:
            /* S8SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_subi_i64(tmp, tmp, lit);
                    else
                       tcg_gen_sub_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                }
            }
            break;
        case 0x1D:
            /* CMPULT */
            gen_cmp(TCG_COND_LTU, ra, rb, rc, islit, lit);
            break;
        case 0x20:
            /* ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x22:
            /* S4ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x29:
            /* SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x2B:
            /* S4SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x2D:
            /* CMPEQ */
            gen_cmp(TCG_COND_EQ, ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* S8ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x3B:
            /* S8SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x3D:
            /* CMPULE */
            gen_cmp(TCG_COND_LEU, ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* ADDL/V */
            gen_addlv(ra, rb, rc, islit, lit);
            break;
        case 0x49:
            /* SUBL/V */
            gen_sublv(ra, rb, rc, islit, lit);
            break;
        case 0x4D:
            /* CMPLT */
            gen_cmp(TCG_COND_LT, ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* ADDQ/V */
            gen_addqv(ra, rb, rc, islit, lit);
            break;
        case 0x69:
            /* SUBQ/V */
            gen_subqv(ra, rb, rc, islit, lit);
            break;
        case 0x6D:
            /* CMPLE */
            gen_cmp(TCG_COND_LE, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x11:
        switch (fn7) {
        case 0x00:
            /* AND */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else if (islit)
                    tcg_gen_andi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                else
                    tcg_gen_and_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
            }
            break;
        case 0x08:
            /* BIC */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_andi_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_andc_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x14:
            /* CMOVLBS */
            gen_cmov(TCG_COND_NE, ra, rb, rc, islit, lit, 1);
            break;
        case 0x16:
            /* CMOVLBC */
            gen_cmov(TCG_COND_EQ, ra, rb, rc, islit, lit, 1);
            break;
        case 0x20:
            /* BIS */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_ori_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_or_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x24:
            /* CMOVEQ */
            gen_cmov(TCG_COND_EQ, ra, rb, rc, islit, lit, 0);
            break;
        case 0x26:
            /* CMOVNE */
            gen_cmov(TCG_COND_NE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x28:
            /* ORNOT */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_ori_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_orc_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], ~lit);
                    else
                        tcg_gen_not_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x40:
            /* XOR */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_xori_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_xor_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x44:
            /* CMOVLT */
            gen_cmov(TCG_COND_LT, ra, rb, rc, islit, lit, 0);
            break;
        case 0x46:
            /* CMOVGE */
            gen_cmov(TCG_COND_GE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x48:
            /* EQV */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_xori_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_eqv_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], ~lit);
                    else
                        tcg_gen_not_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x61:
            /* AMASK */
            if (likely(rc != 31)) {
                uint64_t amask = ctx->tb->flags >> TB_FLAGS_AMASK_SHIFT;

                if (islit) {
                    tcg_gen_movi_i64(cpu_ir[rc], lit & ~amask);
                } else {
                    tcg_gen_andi_i64(cpu_ir[rc], cpu_ir[rb], ~amask);
                }
            }
            break;
        case 0x64:
            /* CMOVLE */
            gen_cmov(TCG_COND_LE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x66:
            /* CMOVGT */
            gen_cmov(TCG_COND_GT, ra, rb, rc, islit, lit, 0);
            break;
        case 0x6C:
            /* IMPLVER */
            if (rc != 31)
                tcg_gen_movi_i64(cpu_ir[rc], ctx->env->implver);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x12:
        switch (fn7) {
        case 0x02:
            /* MSKBL */
            gen_msk_l(ra, rb, rc, islit, lit, 0x01);
            break;
        case 0x06:
            /* EXTBL */
            gen_ext_l(ra, rb, rc, islit, lit, 0x01);
            break;
        case 0x0B:
            /* INSBL */
            gen_ins_l(ra, rb, rc, islit, lit, 0x01);
            break;
        case 0x12:
            /* MSKWL */
            gen_msk_l(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x16:
            /* EXTWL */
            gen_ext_l(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x1B:
            /* INSWL */
            gen_ins_l(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x22:
            /* MSKLL */
            gen_msk_l(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x26:
            /* EXTLL */
            gen_ext_l(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x2B:
            /* INSLL */
            gen_ins_l(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x30:
            /* ZAP */
            gen_zap(ra, rb, rc, islit, lit);
            break;
        case 0x31:
            /* ZAPNOT */
            gen_zapnot(ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* MSKQL */
            gen_msk_l(ra, rb, rc, islit, lit, 0xff);
            break;
        case 0x34:
            /* SRL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_shri_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_shr_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x36:
            /* EXTQL */
            gen_ext_l(ra, rb, rc, islit, lit, 0xff);
            break;
        case 0x39:
            /* SLL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_shli_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_shl_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x3B:
            /* INSQL */
            gen_ins_l(ra, rb, rc, islit, lit, 0xff);
            break;
        case 0x3C:
            /* SRA */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_sari_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_sar_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x52:
            /* MSKWH */
            gen_msk_h(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x57:
            /* INSWH */
            gen_ins_h(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x5A:
            /* EXTWH */
            gen_ext_h(ra, rb, rc, islit, lit, 0x03);
            break;
        case 0x62:
            /* MSKLH */
            gen_msk_h(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x67:
            /* INSLH */
            gen_ins_h(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x6A:
            /* EXTLH */
            gen_ext_h(ra, rb, rc, islit, lit, 0x0f);
            break;
        case 0x72:
            /* MSKQH */
            gen_msk_h(ra, rb, rc, islit, lit, 0xff);
            break;
        case 0x77:
            /* INSQH */
            gen_ins_h(ra, rb, rc, islit, lit, 0xff);
            break;
        case 0x7A:
            /* EXTQH */
            gen_ext_h(ra, rb, rc, islit, lit, 0xff);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x13:
        switch (fn7) {
        case 0x00:
            /* MULL */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else {
                    if (islit)
                        tcg_gen_muli_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_mul_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                }
            }
            break;
        case 0x20:
            /* MULQ */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else if (islit)
                    tcg_gen_muli_i64(cpu_ir[rc], cpu_ir[ra], lit);
                else
                    tcg_gen_mul_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
            }
            break;
        case 0x30:
            /* UMULH */
            gen_umulh(ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* MULL/V */
            gen_mullv(ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* MULQ/V */
            gen_mulqv(ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x14:
        switch (fpfn) { /* fn11 & 0x3F */
        case 0x04:
            /* ITOFS */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_FIX) == 0) {
                goto invalid_opc;
            }
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_trunc_i64_i32(tmp, cpu_ir[ra]);
                    gen_helper_memory_to_s(cpu_fir[rc], tmp);
                    tcg_temp_free_i32(tmp);
                } else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x0A:
            /* SQRTF */
            if (ctx->tb->flags & TB_FLAGS_AMASK_FIX) {
                gen_fsqrtf(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x0B:
            /* SQRTS */
            if (ctx->tb->flags & TB_FLAGS_AMASK_FIX) {
                gen_fsqrts(ctx, rb, rc, fn11);
                break;
            }
            goto invalid_opc;
        case 0x14:
            /* ITOFF */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_FIX) == 0) {
                goto invalid_opc;
            }
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_trunc_i64_i32(tmp, cpu_ir[ra]);
                    gen_helper_memory_to_f(cpu_fir[rc], tmp);
                    tcg_temp_free_i32(tmp);
                } else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x24:
            /* ITOFT */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_FIX) == 0) {
                goto invalid_opc;
            }
            if (likely(rc != 31)) {
                if (ra != 31)
                    tcg_gen_mov_i64(cpu_fir[rc], cpu_ir[ra]);
                else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x2A:
            /* SQRTG */
            if (ctx->tb->flags & TB_FLAGS_AMASK_FIX) {
                gen_fsqrtg(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x02B:
            /* SQRTT */
            if (ctx->tb->flags & TB_FLAGS_AMASK_FIX) {
                gen_fsqrtt(ctx, rb, rc, fn11);
                break;
            }
            goto invalid_opc;
        default:
            goto invalid_opc;
        }
        break;
    case 0x15:
        /* VAX floating point */
        /* XXX: rounding mode and trap are ignored (!) */
        switch (fpfn) { /* fn11 & 0x3F */
        case 0x00:
            /* ADDF */
            gen_faddf(ra, rb, rc);
            break;
        case 0x01:
            /* SUBF */
            gen_fsubf(ra, rb, rc);
            break;
        case 0x02:
            /* MULF */
            gen_fmulf(ra, rb, rc);
            break;
        case 0x03:
            /* DIVF */
            gen_fdivf(ra, rb, rc);
            break;
        case 0x1E:
            /* CVTDG */
#if 0 // TODO
            gen_fcvtdg(rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x20:
            /* ADDG */
            gen_faddg(ra, rb, rc);
            break;
        case 0x21:
            /* SUBG */
            gen_fsubg(ra, rb, rc);
            break;
        case 0x22:
            /* MULG */
            gen_fmulg(ra, rb, rc);
            break;
        case 0x23:
            /* DIVG */
            gen_fdivg(ra, rb, rc);
            break;
        case 0x25:
            /* CMPGEQ */
            gen_fcmpgeq(ra, rb, rc);
            break;
        case 0x26:
            /* CMPGLT */
            gen_fcmpglt(ra, rb, rc);
            break;
        case 0x27:
            /* CMPGLE */
            gen_fcmpgle(ra, rb, rc);
            break;
        case 0x2C:
            /* CVTGF */
            gen_fcvtgf(rb, rc);
            break;
        case 0x2D:
            /* CVTGD */
#if 0 // TODO
            gen_fcvtgd(rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x2F:
            /* CVTGQ */
            gen_fcvtgq(rb, rc);
            break;
        case 0x3C:
            /* CVTQF */
            gen_fcvtqf(rb, rc);
            break;
        case 0x3E:
            /* CVTQG */
            gen_fcvtqg(rb, rc);
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
            gen_fadds(ctx, ra, rb, rc, fn11);
            break;
        case 0x01:
            /* SUBS */
            gen_fsubs(ctx, ra, rb, rc, fn11);
            break;
        case 0x02:
            /* MULS */
            gen_fmuls(ctx, ra, rb, rc, fn11);
            break;
        case 0x03:
            /* DIVS */
            gen_fdivs(ctx, ra, rb, rc, fn11);
            break;
        case 0x20:
            /* ADDT */
            gen_faddt(ctx, ra, rb, rc, fn11);
            break;
        case 0x21:
            /* SUBT */
            gen_fsubt(ctx, ra, rb, rc, fn11);
            break;
        case 0x22:
            /* MULT */
            gen_fmult(ctx, ra, rb, rc, fn11);
            break;
        case 0x23:
            /* DIVT */
            gen_fdivt(ctx, ra, rb, rc, fn11);
            break;
        case 0x24:
            /* CMPTUN */
            gen_fcmptun(ctx, ra, rb, rc, fn11);
            break;
        case 0x25:
            /* CMPTEQ */
            gen_fcmpteq(ctx, ra, rb, rc, fn11);
            break;
        case 0x26:
            /* CMPTLT */
            gen_fcmptlt(ctx, ra, rb, rc, fn11);
            break;
        case 0x27:
            /* CMPTLE */
            gen_fcmptle(ctx, ra, rb, rc, fn11);
            break;
        case 0x2C:
            if (fn11 == 0x2AC || fn11 == 0x6AC) {
                /* CVTST */
                gen_fcvtst(ctx, rb, rc, fn11);
            } else {
                /* CVTTS */
                gen_fcvtts(ctx, rb, rc, fn11);
            }
            break;
        case 0x2F:
            /* CVTTQ */
            gen_fcvttq(ctx, rb, rc, fn11);
            break;
        case 0x3C:
            /* CVTQS */
            gen_fcvtqs(ctx, rb, rc, fn11);
            break;
        case 0x3E:
            /* CVTQT */
            gen_fcvtqt(ctx, rb, rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x17:
        switch (fn11) {
        case 0x010:
            /* CVTLQ */
            gen_fcvtlq(rb, rc);
            break;
        case 0x020:
            if (likely(rc != 31)) {
                if (ra == rb) {
                    /* FMOV */
                    if (ra == 31)
                        tcg_gen_movi_i64(cpu_fir[rc], 0);
                    else
                        tcg_gen_mov_i64(cpu_fir[rc], cpu_fir[ra]);
                } else {
                    /* CPYS */
                    gen_fcpys(ra, rb, rc);
                }
            }
            break;
        case 0x021:
            /* CPYSN */
            gen_fcpysn(ra, rb, rc);
            break;
        case 0x022:
            /* CPYSE */
            gen_fcpyse(ra, rb, rc);
            break;
        case 0x024:
            /* MT_FPCR */
            if (likely(ra != 31))
                gen_helper_store_fpcr(cpu_fir[ra]);
            else {
                TCGv tmp = tcg_const_i64(0);
                gen_helper_store_fpcr(tmp);
                tcg_temp_free(tmp);
            }
            break;
        case 0x025:
            /* MF_FPCR */
            if (likely(ra != 31))
                gen_helper_load_fpcr(cpu_fir[ra]);
            break;
        case 0x02A:
            /* FCMOVEQ */
            gen_fcmov(TCG_COND_EQ, ra, rb, rc);
            break;
        case 0x02B:
            /* FCMOVNE */
            gen_fcmov(TCG_COND_NE, ra, rb, rc);
            break;
        case 0x02C:
            /* FCMOVLT */
            gen_fcmov(TCG_COND_LT, ra, rb, rc);
            break;
        case 0x02D:
            /* FCMOVGE */
            gen_fcmov(TCG_COND_GE, ra, rb, rc);
            break;
        case 0x02E:
            /* FCMOVLE */
            gen_fcmov(TCG_COND_LE, ra, rb, rc);
            break;
        case 0x02F:
            /* FCMOVGT */
            gen_fcmov(TCG_COND_GT, ra, rb, rc);
            break;
        case 0x030:
            /* CVTQL */
            gen_fcvtql(rb, rc);
            break;
        case 0x130:
            /* CVTQL/V */
        case 0x530:
            /* CVTQL/SV */
            /* ??? I'm pretty sure there's nothing that /sv needs to do that
               /v doesn't do.  The only thing I can think is that /sv is a
               valid instruction merely for completeness in the ISA.  */
            gen_fcvtql_v(ctx, rb, rc);
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
            /* No-op */
            break;
        case 0x4400:
            /* WMB */
            /* No-op */
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
            if (ra != 31)
                gen_helper_load_pcc(cpu_ir[ra]);
            break;
        case 0xE000:
            /* RC */
            gen_rx(ra, 0);
            break;
        case 0xE800:
            /* ECB */
            break;
        case 0xF000:
            /* RS */
            gen_rx(ra, 1);
            break;
        case 0xF800:
            /* WH64 */
            /* No-op */
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x19:
        /* HW_MFPR (PALcode) */
#ifndef CONFIG_USER_ONLY
        if (ctx->tb->flags & TB_FLAGS_PAL_MODE) {
            gen_mfpr(ra, insn & 0xffff);
            break;
        }
#endif
        goto invalid_opc;
    case 0x1A:
        /* JMP, JSR, RET, JSR_COROUTINE.  These only differ by the branch
           prediction stack action, which of course we don't implement.  */
        if (rb != 31) {
            tcg_gen_andi_i64(cpu_pc, cpu_ir[rb], ~3);
        } else {
            tcg_gen_movi_i64(cpu_pc, 0);
        }
        if (ra != 31) {
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        }
        ret = EXIT_PC_UPDATED;
        break;
    case 0x1B:
        /* HW_LD (PALcode) */
#ifndef CONFIG_USER_ONLY
        if (ctx->tb->flags & TB_FLAGS_PAL_MODE) {
            TCGv addr;

            if (ra == 31) {
                break;
            }

            addr = tcg_temp_new();
            if (rb != 31)
                tcg_gen_addi_i64(addr, cpu_ir[rb], disp12);
            else
                tcg_gen_movi_i64(addr, disp12);
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access (hw_ldl/p) */
                gen_helper_ldl_phys(cpu_ir[ra], addr);
                break;
            case 0x1:
                /* Quadword physical access (hw_ldq/p) */
                gen_helper_ldq_phys(cpu_ir[ra], addr);
                break;
            case 0x2:
                /* Longword physical access with lock (hw_ldl_l/p) */
                gen_helper_ldl_l_phys(cpu_ir[ra], addr);
                break;
            case 0x3:
                /* Quadword physical access with lock (hw_ldq_l/p) */
                gen_helper_ldq_l_phys(cpu_ir[ra], addr);
                break;
            case 0x4:
                /* Longword virtual PTE fetch (hw_ldl/v) */
                goto invalid_opc;
            case 0x5:
                /* Quadword virtual PTE fetch (hw_ldq/v) */
                goto invalid_opc;
                break;
            case 0x6:
                /* Incpu_ir[ra]id */
                goto invalid_opc;
            case 0x7:
                /* Incpu_ir[ra]id */
                goto invalid_opc;
            case 0x8:
                /* Longword virtual access (hw_ldl) */
                goto invalid_opc;
            case 0x9:
                /* Quadword virtual access (hw_ldq) */
                goto invalid_opc;
            case 0xA:
                /* Longword virtual access with protection check (hw_ldl/w) */
                tcg_gen_qemu_ld32s(cpu_ir[ra], addr, MMU_KERNEL_IDX);
                break;
            case 0xB:
                /* Quadword virtual access with protection check (hw_ldq/w) */
                tcg_gen_qemu_ld64(cpu_ir[ra], addr, MMU_KERNEL_IDX);
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
                tcg_gen_qemu_ld32s(cpu_ir[ra], addr, MMU_USER_IDX);
                break;
            case 0xF:
                /* Quadword virtual access with alternate access mode and
                   protection checks (hw_ldq/wa) */
                tcg_gen_qemu_ld64(cpu_ir[ra], addr, MMU_USER_IDX);
                break;
            }
            tcg_temp_free(addr);
            break;
        }
#endif
        goto invalid_opc;
    case 0x1C:
        switch (fn7) {
        case 0x00:
            /* SEXTB */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_BWX) == 0) {
                goto invalid_opc;
            }
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], (int64_t)((int8_t)lit));
                else
                    tcg_gen_ext8s_i64(cpu_ir[rc], cpu_ir[rb]);
            }
            break;
        case 0x01:
            /* SEXTW */
            if (ctx->tb->flags & TB_FLAGS_AMASK_BWX) {
                if (likely(rc != 31)) {
                    if (islit) {
                        tcg_gen_movi_i64(cpu_ir[rc], (int64_t)((int16_t)lit));
                    } else {
                        tcg_gen_ext16s_i64(cpu_ir[rc], cpu_ir[rb]);
                    }
                }
                break;
            }
            goto invalid_opc;
        case 0x30:
            /* CTPOP */
            if (ctx->tb->flags & TB_FLAGS_AMASK_CIX) {
                if (likely(rc != 31)) {
                    if (islit) {
                        tcg_gen_movi_i64(cpu_ir[rc], ctpop64(lit));
                    } else {
                        gen_helper_ctpop(cpu_ir[rc], cpu_ir[rb]);
                    }
                }
                break;
            }
            goto invalid_opc;
        case 0x31:
            /* PERR */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_perr(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x32:
            /* CTLZ */
            if (ctx->tb->flags & TB_FLAGS_AMASK_CIX) {
                if (likely(rc != 31)) {
                    if (islit) {
                        tcg_gen_movi_i64(cpu_ir[rc], clz64(lit));
                    } else {
                        gen_helper_ctlz(cpu_ir[rc], cpu_ir[rb]);
                    }
                }
                break;
            }
            goto invalid_opc;
        case 0x33:
            /* CTTZ */
            if (ctx->tb->flags & TB_FLAGS_AMASK_CIX) {
                if (likely(rc != 31)) {
                    if (islit) {
                        tcg_gen_movi_i64(cpu_ir[rc], ctz64(lit));
                    } else {
                        gen_helper_cttz(cpu_ir[rc], cpu_ir[rb]);
                    }
                }
                break;
            }
            goto invalid_opc;
        case 0x34:
            /* UNPKBW */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                if (real_islit || ra != 31) {
                    goto invalid_opc;
                }
                gen_unpkbw(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x35:
            /* UNPKBL */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                if (real_islit || ra != 31) {
                    goto invalid_opc;
                }
                gen_unpkbl(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x36:
            /* PKWB */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                if (real_islit || ra != 31) {
                    goto invalid_opc;
                }
                gen_pkwb(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x37:
            /* PKLB */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                if (real_islit || ra != 31) {
                    goto invalid_opc;
                }
                gen_pklb(rb, rc);
                break;
            }
            goto invalid_opc;
        case 0x38:
            /* MINSB8 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_minsb8(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x39:
            /* MINSW4 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_minsw4(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3A:
            /* MINUB8 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_minub8(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3B:
            /* MINUW4 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_minuw4(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3C:
            /* MAXUB8 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_maxub8(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3D:
            /* MAXUW4 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_maxuw4(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3E:
            /* MAXSB8 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_maxsb8(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x3F:
            /* MAXSW4 */
            if (ctx->tb->flags & TB_FLAGS_AMASK_MVI) {
                gen_maxsw4(ra, rb, rc, islit, lit);
                break;
            }
            goto invalid_opc;
        case 0x70:
            /* FTOIT */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_FIX) == 0) {
                goto invalid_opc;
            }
            if (likely(rc != 31)) {
                if (ra != 31)
                    tcg_gen_mov_i64(cpu_ir[rc], cpu_fir[ra]);
                else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x78:
            /* FTOIS */
            if ((ctx->tb->flags & TB_FLAGS_AMASK_FIX) == 0) {
                goto invalid_opc;
            }
            if (rc != 31) {
                TCGv_i32 tmp1 = tcg_temp_new_i32();
                if (ra != 31)
                    gen_helper_s_to_memory(tmp1, cpu_fir[ra]);
                else {
                    TCGv tmp2 = tcg_const_i64(0);
                    gen_helper_s_to_memory(tmp1, tmp2);
                    tcg_temp_free(tmp2);
                }
                tcg_gen_ext_i32_i64(cpu_ir[rc], tmp1);
                tcg_temp_free_i32(tmp1);
            }
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x1D:
        /* HW_MTPR (PALcode) */
#ifndef CONFIG_USER_ONLY
        if (ctx->tb->flags & TB_FLAGS_PAL_MODE) {
            gen_mtpr(rb, insn & 0xffff);
            break;
        }
#endif
        goto invalid_opc;
    case 0x1E:
        /* HW_RET (PALcode) */
#ifndef CONFIG_USER_ONLY
        if (ctx->tb->flags & TB_FLAGS_PAL_MODE) {
            if (rb == 31) {
                /* Pre-EV6 CPUs interpreted this as HW_REI, loading the return
                   address from EXC_ADDR.  This turns out to be useful for our
                   emulation PALcode, so continue to accept it.  */
                TCGv tmp = tcg_temp_new();
                tcg_gen_ld_i64(tmp, cpu_env, offsetof(CPUState, exc_addr));
                gen_helper_hw_ret(tmp);
                tcg_temp_free(tmp);
            } else {
                gen_helper_hw_ret(cpu_ir[rb]);
            }
            ret = EXIT_PC_UPDATED;
            break;
        }
#endif
        goto invalid_opc;
    case 0x1F:
        /* HW_ST (PALcode) */
#ifndef CONFIG_USER_ONLY
        if (ctx->tb->flags & TB_FLAGS_PAL_MODE) {
            TCGv addr, val;
            addr = tcg_temp_new();
            if (rb != 31)
                tcg_gen_addi_i64(addr, cpu_ir[rb], disp12);
            else
                tcg_gen_movi_i64(addr, disp12);
            if (ra != 31)
                val = cpu_ir[ra];
            else {
                val = tcg_temp_new();
                tcg_gen_movi_i64(val, 0);
            }
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access */
                gen_helper_stl_phys(addr, val);
                break;
            case 0x1:
                /* Quadword physical access */
                gen_helper_stq_phys(addr, val);
                break;
            case 0x2:
                /* Longword physical access with lock */
                gen_helper_stl_c_phys(val, addr, val);
                break;
            case 0x3:
                /* Quadword physical access with lock */
                gen_helper_stq_c_phys(val, addr, val);
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
            if (ra == 31)
                tcg_temp_free(val);
            tcg_temp_free(addr);
            break;
        }
#endif
        goto invalid_opc;
    case 0x20:
        /* LDF */
        gen_load_mem(ctx, &gen_qemu_ldf, ra, rb, disp16, 1, 0);
        break;
    case 0x21:
        /* LDG */
        gen_load_mem(ctx, &gen_qemu_ldg, ra, rb, disp16, 1, 0);
        break;
    case 0x22:
        /* LDS */
        gen_load_mem(ctx, &gen_qemu_lds, ra, rb, disp16, 1, 0);
        break;
    case 0x23:
        /* LDT */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 1, 0);
        break;
    case 0x24:
        /* STF */
        gen_store_mem(ctx, &gen_qemu_stf, ra, rb, disp16, 1, 0);
        break;
    case 0x25:
        /* STG */
        gen_store_mem(ctx, &gen_qemu_stg, ra, rb, disp16, 1, 0);
        break;
    case 0x26:
        /* STS */
        gen_store_mem(ctx, &gen_qemu_sts, ra, rb, disp16, 1, 0);
        break;
    case 0x27:
        /* STT */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 1, 0);
        break;
    case 0x28:
        /* LDL */
        gen_load_mem(ctx, &tcg_gen_qemu_ld32s, ra, rb, disp16, 0, 0);
        break;
    case 0x29:
        /* LDQ */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 0, 0);
        break;
    case 0x2A:
        /* LDL_L */
        gen_load_mem(ctx, &gen_qemu_ldl_l, ra, rb, disp16, 0, 0);
        break;
    case 0x2B:
        /* LDQ_L */
        gen_load_mem(ctx, &gen_qemu_ldq_l, ra, rb, disp16, 0, 0);
        break;
    case 0x2C:
        /* STL */
        gen_store_mem(ctx, &tcg_gen_qemu_st32, ra, rb, disp16, 0, 0);
        break;
    case 0x2D:
        /* STQ */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 0, 0);
        break;
    case 0x2E:
        /* STL_C */
        ret = gen_store_conditional(ctx, ra, rb, disp16, 0);
        break;
    case 0x2F:
        /* STQ_C */
        ret = gen_store_conditional(ctx, ra, rb, disp16, 1);
        break;
    case 0x30:
        /* BR */
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x31: /* FBEQ */
        ret = gen_fbcond(ctx, TCG_COND_EQ, ra, disp21);
        break;
    case 0x32: /* FBLT */
        ret = gen_fbcond(ctx, TCG_COND_LT, ra, disp21);
        break;
    case 0x33: /* FBLE */
        ret = gen_fbcond(ctx, TCG_COND_LE, ra, disp21);
        break;
    case 0x34:
        /* BSR */
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x35: /* FBNE */
        ret = gen_fbcond(ctx, TCG_COND_NE, ra, disp21);
        break;
    case 0x36: /* FBGE */
        ret = gen_fbcond(ctx, TCG_COND_GE, ra, disp21);
        break;
    case 0x37: /* FBGT */
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
    }

    return ret;
}

static inline void gen_intermediate_code_internal(CPUState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
    DisasContext ctx, *ctxp = &ctx;
    target_ulong pc_start;
    uint32_t insn;
    uint16_t *gen_opc_end;
    CPUBreakpoint *bp;
    int j, lj = -1;
    ExitStatus ret;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

    ctx.tb = tb;
    ctx.env = env;
    ctx.pc = pc_start;
    ctx.mem_idx = cpu_mmu_index(env);

    /* ??? Every TB begins with unset rounding mode, to be initialized on
       the first fp insn of the TB.  Alternately we could define a proper
       default for every TB (e.g. QUAL_RM_N or QUAL_RM_D) and make sure
       to reset the FP_STATUS to that default at the end of any TB that
       changes the default.  We could even (gasp) dynamiclly figure out
       what default would be most efficient given the running program.  */
    ctx.tb_rm = -1;
    /* Similarly for flush-to-zero.  */
    ctx.tb_ftz = -1;

    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_icount_start();
    do {
        if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
            QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == ctx.pc) {
                    gen_excp(&ctx, EXCP_DEBUG, 0);
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = ctx.pc;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();
        insn = ldl_code(ctx.pc);
        num_insns++;

	if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP))) {
            tcg_gen_debug_insn_start(ctx.pc);
        }

        ctx.pc += 4;
        ret = translate_one(ctxp, insn);

        /* If we reach a page boundary, are single stepping,
           or exhaust instruction count, stop generation.  */
        if (ret == NO_EXIT
            && ((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0
                || gen_opc_ptr >= gen_opc_end
                || num_insns >= max_insns
                || singlestep
                || env->singlestep_enabled)) {
            ret = EXIT_PC_STALE;
        }
    } while (ret == NO_EXIT);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    switch (ret) {
    case EXIT_GOTO_TB:
    case EXIT_NORETURN:
        break;
    case EXIT_PC_STALE:
        tcg_gen_movi_i64(cpu_pc, ctx.pc);
        /* FALLTHRU */
    case EXIT_PC_UPDATED:
        if (env->singlestep_enabled) {
            gen_excp_1(EXCP_DEBUG, 0);
        } else {
            tcg_gen_exit_tb(0);
        }
        break;
    default:
        abort();
    }

    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = ctx.pc - pc_start;
        tb->icount = num_insns;
    }

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(pc_start, ctx.pc - pc_start, 1);
        qemu_log("\n");
    }
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

struct cpu_def_t {
    const char *name;
    int implver, amask;
};

static const struct cpu_def_t cpu_defs[] = {
    { "ev4",   IMPLVER_2106x, 0 },
    { "ev5",   IMPLVER_21164, 0 },
    { "ev56",  IMPLVER_21164, AMASK_BWX },
    { "pca56", IMPLVER_21164, AMASK_BWX | AMASK_MVI },
    { "ev6",   IMPLVER_21264, AMASK_BWX | AMASK_FIX | AMASK_MVI | AMASK_TRAP },
    { "ev67",  IMPLVER_21264, (AMASK_BWX | AMASK_FIX | AMASK_CIX
			       | AMASK_MVI | AMASK_TRAP | AMASK_PREFETCH), },
    { "ev68",  IMPLVER_21264, (AMASK_BWX | AMASK_FIX | AMASK_CIX
			       | AMASK_MVI | AMASK_TRAP | AMASK_PREFETCH), },
    { "21064", IMPLVER_2106x, 0 },
    { "21164", IMPLVER_21164, 0 },
    { "21164a", IMPLVER_21164, AMASK_BWX },
    { "21164pc", IMPLVER_21164, AMASK_BWX | AMASK_MVI },
    { "21264", IMPLVER_21264, AMASK_BWX | AMASK_FIX | AMASK_MVI | AMASK_TRAP },
    { "21264a", IMPLVER_21264, (AMASK_BWX | AMASK_FIX | AMASK_CIX
				| AMASK_MVI | AMASK_TRAP | AMASK_PREFETCH), }
};

CPUAlphaState * cpu_alpha_init (const char *cpu_model)
{
    CPUAlphaState *env;
    int implver, amask, i, max;

    env = qemu_mallocz(sizeof(CPUAlphaState));
    cpu_exec_init(env);
    alpha_translate_init();
    tlb_flush(env, 1);

    /* Default to ev67; no reason not to emulate insns by default.  */
    implver = IMPLVER_21264;
    amask = (AMASK_BWX | AMASK_FIX | AMASK_CIX | AMASK_MVI
	     | AMASK_TRAP | AMASK_PREFETCH);

    max = ARRAY_SIZE(cpu_defs);
    for (i = 0; i < max; i++) {
        if (strcmp (cpu_model, cpu_defs[i].name) == 0) {
            implver = cpu_defs[i].implver;
            amask = cpu_defs[i].amask;
            break;
        }
    }
    env->implver = implver;
    env->amask = amask;

#if defined (CONFIG_USER_ONLY)
    env->ps = PS_USER_MODE;
    cpu_alpha_store_fpcr(env, (FPCR_INVD | FPCR_DZED | FPCR_OVFD
                               | FPCR_UNFD | FPCR_INED | FPCR_DNOD));
#endif
    env->lock_addr = -1;
    env->fen = 1;

    qemu_init_vcpu(env);
    return env;
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, int pc_pos)
{
    env->pc = gen_opc_pc[pc_pos];
}
