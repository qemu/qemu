/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include <math.h>
#include "cpu.h"
#include "tcg-cpu.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-macros.h"
#include "helper-tcg.h"

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt].d)
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST1    ST(1)

#define FPU_RC_SHIFT        10
#define FPU_RC_MASK         (3 << FPU_RC_SHIFT)
#define FPU_RC_NEAR         0x000
#define FPU_RC_DOWN         0x400
#define FPU_RC_UP           0x800
#define FPU_RC_CHOP         0xc00

#define MAXTAN 9223372036854775808.0

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)        (fp.l.upper & 0x7fff)
#define SIGND(fp)       ((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#define FPUS_B  (1 << 15)

#define FPUC_EM 0x3f

#define floatx80_lg2 make_floatx80(0x3ffd, 0x9a209a84fbcff799LL)
#define floatx80_lg2_d make_floatx80(0x3ffd, 0x9a209a84fbcff798LL)
#define floatx80_l2e make_floatx80(0x3fff, 0xb8aa3b295c17f0bcLL)
#define floatx80_l2e_d make_floatx80(0x3fff, 0xb8aa3b295c17f0bbLL)
#define floatx80_l2t make_floatx80(0x4000, 0xd49a784bcd1b8afeLL)
#define floatx80_l2t_u make_floatx80(0x4000, 0xd49a784bcd1b8affLL)
#define floatx80_ln2_d make_floatx80(0x3ffe, 0xb17217f7d1cf79abLL)
#define floatx80_pi_d make_floatx80(0x4000, 0xc90fdaa22168c234LL)

static inline void fpush(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(CPUX86State *env)
{
    env->fptags[env->fpstt] = 1; /* invalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

static floatx80 do_fldt(CPUX86State *env, target_ulong ptr, uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.l.lower = cpu_ldq_data_ra(env, ptr, retaddr);
    temp.l.upper = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    return temp.d;
}

static void do_fstt(CPUX86State *env, floatx80 f, target_ulong ptr,
                    uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.d = f;
    cpu_stq_data_ra(env, ptr, temp.l.lower, retaddr);
    cpu_stw_data_ra(env, ptr + 8, temp.l.upper, retaddr);
}

/* x87 FPU helpers */

static inline double floatx80_to_double(CPUX86State *env, floatx80 a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.f64 = floatx80_to_float64(a, &env->fp_status);
    return u.d;
}

static inline floatx80 double_to_floatx80(CPUX86State *env, double a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.d = a;
    return float64_to_floatx80(u.f64, &env->fp_status);
}

static void fpu_set_exception(CPUX86State *env, int mask)
{
    env->fpus |= mask;
    if (env->fpus & (~env->fpuc & FPUC_EM)) {
        env->fpus |= FPUS_SE | FPUS_B;
    }
}

static inline uint8_t save_exception_flags(CPUX86State *env)
{
    uint8_t old_flags = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);
    return old_flags;
}

static void merge_exception_flags(CPUX86State *env, uint8_t old_flags)
{
    uint8_t new_flags = get_float_exception_flags(&env->fp_status);
    float_raise(old_flags, &env->fp_status);
    fpu_set_exception(env,
                      ((new_flags & float_flag_invalid ? FPUS_IE : 0) |
                       (new_flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                       (new_flags & float_flag_overflow ? FPUS_OE : 0) |
                       (new_flags & float_flag_underflow ? FPUS_UE : 0) |
                       (new_flags & float_flag_inexact ? FPUS_PE : 0) |
                       (new_flags & float_flag_input_denormal ? FPUS_DE : 0)));
}

static inline floatx80 helper_fdiv(CPUX86State *env, floatx80 a, floatx80 b)
{
    uint8_t old_flags = save_exception_flags(env);
    floatx80 ret = floatx80_div(a, b, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return ret;
}

static void fpu_raise_exception(CPUX86State *env, uintptr_t retaddr)
{
    if (env->cr[0] & CR0_NE_MASK) {
        raise_exception_ra(env, EXCP10_COPR, retaddr);
    }
#if !defined(CONFIG_USER_ONLY)
    else {
        fpu_check_raise_ferr_irq(env);
    }
#endif
}

void helper_flds_FT0(CPUX86State *env, uint32_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.i = val;
    FT0 = float32_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fldl_FT0(CPUX86State *env, uint64_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.i = val;
    FT0 = float64_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fildl_FT0(CPUX86State *env, int32_t val)
{
    FT0 = int32_to_floatx80(val, &env->fp_status);
}

void helper_flds_ST0(CPUX86State *env, uint32_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float32 f;
        uint32_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
    env->fpregs[new_fpstt].d = float32_to_floatx80(u.f, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

void helper_fldl_ST0(CPUX86State *env, uint64_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float64 f;
        uint64_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
    env->fpregs[new_fpstt].d = float64_to_floatx80(u.f, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

static FloatX80RoundPrec tmp_maximise_precision(float_status *st)
{
    FloatX80RoundPrec old = get_floatx80_rounding_precision(st);
    set_floatx80_rounding_precision(floatx80_precision_x, st);
    return old;
}

void helper_fildl_ST0(CPUX86State *env, int32_t val)
{
    int new_fpstt;
    FloatX80RoundPrec old = tmp_maximise_precision(&env->fp_status);

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int32_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */

    set_floatx80_rounding_precision(old, &env->fp_status);
}

void helper_fildll_ST0(CPUX86State *env, int64_t val)
{
    int new_fpstt;
    FloatX80RoundPrec old = tmp_maximise_precision(&env->fp_status);

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int64_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */

    set_floatx80_rounding_precision(old, &env->fp_status);
}

uint32_t helper_fsts_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.f = floatx80_to_float32(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return u.i;
}

uint64_t helper_fstl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.f = floatx80_to_float64(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return u.i;
}

int32_t helper_fist_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fistll_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistt_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fisttl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fisttll_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

void helper_fldt_ST0(CPUX86State *env, target_ulong ptr)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = do_fldt(env, ptr, GETPC());
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fstt_ST0(CPUX86State *env, target_ulong ptr)
{
    do_fstt(env, ST0, ptr, GETPC());
}

void helper_fpush(CPUX86State *env)
{
    fpush(env);
}

void helper_fpop(CPUX86State *env)
{
    fpop(env);
}

void helper_fdecstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fpus &= ~0x4700;
}

void helper_fincstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt + 1) & 7;
    env->fpus &= ~0x4700;
}

/* FPU move */

void helper_ffree_STN(CPUX86State *env, int st_index)
{
    env->fptags[(env->fpstt + st_index) & 7] = 1;
}

void helper_fmov_ST0_FT0(CPUX86State *env)
{
    ST0 = FT0;
}

void helper_fmov_FT0_STN(CPUX86State *env, int st_index)
{
    FT0 = ST(st_index);
}

void helper_fmov_ST0_STN(CPUX86State *env, int st_index)
{
    ST0 = ST(st_index);
}

void helper_fmov_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = ST0;
}

void helper_fxchg_ST0_STN(CPUX86State *env, int st_index)
{
    floatx80 tmp;

    tmp = ST(st_index);
    ST(st_index) = ST0;
    ST0 = tmp;
}

/* FPU operations */

static const int fcom_ccval[4] = {0x0100, 0x4000, 0x0000, 0x4500};

void helper_fcom_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
}

void helper_fucom_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
}

static const int fcomi_ccval[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_fcomi_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int eflags;
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    merge_exception_flags(env, old_flags);
}

void helper_fucomi_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int eflags;
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    merge_exception_flags(env, old_flags);
}

void helper_fadd_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_add(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fmul_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_mul(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsub_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsubr_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(FT0, ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fdiv_ST0_FT0(CPUX86State *env)
{
    ST0 = helper_fdiv(env, ST0, FT0);
}

void helper_fdivr_ST0_FT0(CPUX86State *env)
{
    ST0 = helper_fdiv(env, FT0, ST0);
}

/* fp operations between STN and ST0 */

void helper_fadd_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_add(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_mul(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsub_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsubr_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST0, ST(st_index), &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fdiv_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, *p, ST0);
}

void helper_fdivr_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, ST0, *p);
}

/* misc FPU operations */
void helper_fchs_ST0(CPUX86State *env)
{
    ST0 = floatx80_chs(ST0);
}

void helper_fabs_ST0(CPUX86State *env)
{
    ST0 = floatx80_abs(ST0);
}

void helper_fld1_ST0(CPUX86State *env)
{
    ST0 = floatx80_one;
}

void helper_fldl2t_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_UP:
        ST0 = floatx80_l2t_u;
        break;
    default:
        ST0 = floatx80_l2t;
        break;
    }
}

void helper_fldl2e_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_l2e_d;
        break;
    default:
        ST0 = floatx80_l2e;
        break;
    }
}

void helper_fldpi_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_pi_d;
        break;
    default:
        ST0 = floatx80_pi;
        break;
    }
}

void helper_fldlg2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_lg2_d;
        break;
    default:
        ST0 = floatx80_lg2;
        break;
    }
}

void helper_fldln2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_ln2_d;
        break;
    default:
        ST0 = floatx80_ln2;
        break;
    }
}

void helper_fldz_ST0(CPUX86State *env)
{
    ST0 = floatx80_zero;
}

void helper_fldz_FT0(CPUX86State *env)
{
    FT0 = floatx80_zero;
}

uint32_t helper_fnstsw(CPUX86State *env)
{
    return (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
}

uint32_t helper_fnstcw(CPUX86State *env)
{
    return env->fpuc;
}

static void set_x86_rounding_mode(unsigned mode, float_status *status)
{
    static FloatRoundMode x86_round_mode[4] = {
        float_round_nearest_even,
        float_round_down,
        float_round_up,
        float_round_to_zero
    };
    assert(mode < ARRAY_SIZE(x86_round_mode));
    set_float_rounding_mode(x86_round_mode[mode], status);
}

void update_fp_status(CPUX86State *env)
{
    int rnd_mode;
    FloatX80RoundPrec rnd_prec;

    /* set rounding mode */
    rnd_mode = (env->fpuc & FPU_RC_MASK) >> FPU_RC_SHIFT;
    set_x86_rounding_mode(rnd_mode, &env->fp_status);

    switch ((env->fpuc >> 8) & 3) {
    case 0:
        rnd_prec = floatx80_precision_s;
        break;
    case 2:
        rnd_prec = floatx80_precision_d;
        break;
    case 3:
    default:
        rnd_prec = floatx80_precision_x;
        break;
    }
    set_floatx80_rounding_precision(rnd_prec, &env->fp_status);
}

void helper_fldcw(CPUX86State *env, uint32_t val)
{
    cpu_set_fpuc(env, val);
}

void helper_fclex(CPUX86State *env)
{
    env->fpus &= 0x7f00;
}

void helper_fwait(CPUX86State *env)
{
    if (env->fpus & FPUS_SE) {
        fpu_raise_exception(env, GETPC());
    }
}

static void do_fninit(CPUX86State *env)
{
    env->fpus = 0;
    env->fpstt = 0;
    env->fpcs = 0;
    env->fpds = 0;
    env->fpip = 0;
    env->fpdp = 0;
    cpu_set_fpuc(env, 0x37f);
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

void helper_fninit(CPUX86State *env)
{
    do_fninit(env);
}

/* BCD ops */

void helper_fbld_ST0(CPUX86State *env, target_ulong ptr)
{
    floatx80 tmp;
    uint64_t val;
    unsigned int v;
    int i;

    val = 0;
    for (i = 8; i >= 0; i--) {
        v = cpu_ldub_data_ra(env, ptr + i, GETPC());
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = int64_to_floatx80(val, &env->fp_status);
    if (cpu_ldub_data_ra(env, ptr + 9, GETPC()) & 0x80) {
        tmp = floatx80_chs(tmp);
    }
    fpush(env);
    ST0 = tmp;
}

void helper_fbst_ST0(CPUX86State *env, target_ulong ptr)
{
    uint8_t old_flags = save_exception_flags(env);
    int v;
    target_ulong mem_ref, mem_end;
    int64_t val;
    CPU_LDoubleU temp;

    temp.d = ST0;

    val = floatx80_to_int64(ST0, &env->fp_status);
    mem_ref = ptr;
    if (val >= 1000000000000000000LL || val <= -1000000000000000000LL) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        while (mem_ref < ptr + 7) {
            cpu_stb_data_ra(env, mem_ref++, 0, GETPC());
        }
        cpu_stb_data_ra(env, mem_ref++, 0xc0, GETPC());
        cpu_stb_data_ra(env, mem_ref++, 0xff, GETPC());
        cpu_stb_data_ra(env, mem_ref++, 0xff, GETPC());
        merge_exception_flags(env, old_flags);
        return;
    }
    mem_end = mem_ref + 9;
    if (SIGND(temp)) {
        cpu_stb_data_ra(env, mem_end, 0x80, GETPC());
        val = -val;
    } else {
        cpu_stb_data_ra(env, mem_end, 0x00, GETPC());
    }
    while (mem_ref < mem_end) {
        if (val == 0) {
            break;
        }
        v = val % 100;
        val = val / 100;
        v = ((v / 10) << 4) | (v % 10);
        cpu_stb_data_ra(env, mem_ref++, v, GETPC());
    }
    while (mem_ref < mem_end) {
        cpu_stb_data_ra(env, mem_ref++, 0, GETPC());
    }
    merge_exception_flags(env, old_flags);
}

/* 128-bit significand of log(2).  */
#define ln2_sig_high 0xb17217f7d1cf79abULL
#define ln2_sig_low 0xc9e3b39803f2f6afULL

/*
 * Polynomial coefficients for an approximation to (2^x - 1) / x, on
 * the interval [-1/64, 1/64].
 */
#define f2xm1_coeff_0 make_floatx80(0x3ffe, 0xb17217f7d1cf79acULL)
#define f2xm1_coeff_0_low make_floatx80(0xbfbc, 0xd87edabf495b3762ULL)
#define f2xm1_coeff_1 make_floatx80(0x3ffc, 0xf5fdeffc162c7543ULL)
#define f2xm1_coeff_2 make_floatx80(0x3ffa, 0xe35846b82505fcc7ULL)
#define f2xm1_coeff_3 make_floatx80(0x3ff8, 0x9d955b7dd273b899ULL)
#define f2xm1_coeff_4 make_floatx80(0x3ff5, 0xaec3ff3c4ef4ac0cULL)
#define f2xm1_coeff_5 make_floatx80(0x3ff2, 0xa184897c3a7f0de9ULL)
#define f2xm1_coeff_6 make_floatx80(0x3fee, 0xffe634d0ec30d504ULL)
#define f2xm1_coeff_7 make_floatx80(0x3feb, 0xb160111d2db515e4ULL)

struct f2xm1_data {
    /*
     * A value very close to a multiple of 1/32, such that 2^t and 2^t - 1
     * are very close to exact floatx80 values.
     */
    floatx80 t;
    /* The value of 2^t.  */
    floatx80 exp2;
    /* The value of 2^t - 1.  */
    floatx80 exp2m1;
};

static const struct f2xm1_data f2xm1_table[65] = {
    { make_floatx80_init(0xbfff, 0x8000000000000000ULL),
      make_floatx80_init(0x3ffe, 0x8000000000000000ULL),
      make_floatx80_init(0xbffe, 0x8000000000000000ULL) },
    { make_floatx80_init(0xbffe, 0xf800000000002e7eULL),
      make_floatx80_init(0x3ffe, 0x82cd8698ac2b9160ULL),
      make_floatx80_init(0xbffd, 0xfa64f2cea7a8dd40ULL) },
    { make_floatx80_init(0xbffe, 0xefffffffffffe960ULL),
      make_floatx80_init(0x3ffe, 0x85aac367cc488345ULL),
      make_floatx80_init(0xbffd, 0xf4aa7930676ef976ULL) },
    { make_floatx80_init(0xbffe, 0xe800000000006f10ULL),
      make_floatx80_init(0x3ffe, 0x88980e8092da5c14ULL),
      make_floatx80_init(0xbffd, 0xeecfe2feda4b47d8ULL) },
    { make_floatx80_init(0xbffe, 0xe000000000008a45ULL),
      make_floatx80_init(0x3ffe, 0x8b95c1e3ea8ba2a5ULL),
      make_floatx80_init(0xbffd, 0xe8d47c382ae8bab6ULL) },
    { make_floatx80_init(0xbffe, 0xd7ffffffffff8a9eULL),
      make_floatx80_init(0x3ffe, 0x8ea4398b45cd8116ULL),
      make_floatx80_init(0xbffd, 0xe2b78ce97464fdd4ULL) },
    { make_floatx80_init(0xbffe, 0xd0000000000019a0ULL),
      make_floatx80_init(0x3ffe, 0x91c3d373ab11b919ULL),
      make_floatx80_init(0xbffd, 0xdc785918a9dc8dceULL) },
    { make_floatx80_init(0xbffe, 0xc7ffffffffff14dfULL),
      make_floatx80_init(0x3ffe, 0x94f4efa8fef76836ULL),
      make_floatx80_init(0xbffd, 0xd61620ae02112f94ULL) },
    { make_floatx80_init(0xbffe, 0xc000000000006530ULL),
      make_floatx80_init(0x3ffe, 0x9837f0518db87fbbULL),
      make_floatx80_init(0xbffd, 0xcf901f5ce48f008aULL) },
    { make_floatx80_init(0xbffe, 0xb7ffffffffff1723ULL),
      make_floatx80_init(0x3ffe, 0x9b8d39b9d54eb74cULL),
      make_floatx80_init(0xbffd, 0xc8e58c8c55629168ULL) },
    { make_floatx80_init(0xbffe, 0xb00000000000b5e1ULL),
      make_floatx80_init(0x3ffe, 0x9ef5326091a0c366ULL),
      make_floatx80_init(0xbffd, 0xc2159b3edcbe7934ULL) },
    { make_floatx80_init(0xbffe, 0xa800000000006f8aULL),
      make_floatx80_init(0x3ffe, 0xa27043030c49370aULL),
      make_floatx80_init(0xbffd, 0xbb1f79f9e76d91ecULL) },
    { make_floatx80_init(0xbffe, 0x9fffffffffff816aULL),
      make_floatx80_init(0x3ffe, 0xa5fed6a9b15171cfULL),
      make_floatx80_init(0xbffd, 0xb40252ac9d5d1c62ULL) },
    { make_floatx80_init(0xbffe, 0x97ffffffffffb621ULL),
      make_floatx80_init(0x3ffe, 0xa9a15ab4ea7c30e6ULL),
      make_floatx80_init(0xbffd, 0xacbd4a962b079e34ULL) },
    { make_floatx80_init(0xbffe, 0x8fffffffffff162bULL),
      make_floatx80_init(0x3ffe, 0xad583eea42a1b886ULL),
      make_floatx80_init(0xbffd, 0xa54f822b7abc8ef4ULL) },
    { make_floatx80_init(0xbffe, 0x87ffffffffff4d34ULL),
      make_floatx80_init(0x3ffe, 0xb123f581d2ac7b51ULL),
      make_floatx80_init(0xbffd, 0x9db814fc5aa7095eULL) },
    { make_floatx80_init(0xbffe, 0x800000000000227dULL),
      make_floatx80_init(0x3ffe, 0xb504f333f9de539dULL),
      make_floatx80_init(0xbffd, 0x95f619980c4358c6ULL) },
    { make_floatx80_init(0xbffd, 0xefffffffffff3978ULL),
      make_floatx80_init(0x3ffe, 0xb8fbaf4762fbd0a1ULL),
      make_floatx80_init(0xbffd, 0x8e08a1713a085ebeULL) },
    { make_floatx80_init(0xbffd, 0xe00000000000df81ULL),
      make_floatx80_init(0x3ffe, 0xbd08a39f580bfd8cULL),
      make_floatx80_init(0xbffd, 0x85eeb8c14fe804e8ULL) },
    { make_floatx80_init(0xbffd, 0xd00000000000bccfULL),
      make_floatx80_init(0x3ffe, 0xc12c4cca667062f6ULL),
      make_floatx80_init(0xbffc, 0xfb4eccd6663e7428ULL) },
    { make_floatx80_init(0xbffd, 0xc00000000000eff0ULL),
      make_floatx80_init(0x3ffe, 0xc5672a1155069abeULL),
      make_floatx80_init(0xbffc, 0xea6357baabe59508ULL) },
    { make_floatx80_init(0xbffd, 0xb000000000000fe6ULL),
      make_floatx80_init(0x3ffe, 0xc9b9bd866e2f234bULL),
      make_floatx80_init(0xbffc, 0xd91909e6474372d4ULL) },
    { make_floatx80_init(0xbffd, 0x9fffffffffff2172ULL),
      make_floatx80_init(0x3ffe, 0xce248c151f84bf00ULL),
      make_floatx80_init(0xbffc, 0xc76dcfab81ed0400ULL) },
    { make_floatx80_init(0xbffd, 0x8fffffffffffafffULL),
      make_floatx80_init(0x3ffe, 0xd2a81d91f12afb2bULL),
      make_floatx80_init(0xbffc, 0xb55f89b83b541354ULL) },
    { make_floatx80_init(0xbffc, 0xffffffffffff81a3ULL),
      make_floatx80_init(0x3ffe, 0xd744fccad69d7d5eULL),
      make_floatx80_init(0xbffc, 0xa2ec0cd4a58a0a88ULL) },
    { make_floatx80_init(0xbffc, 0xdfffffffffff1568ULL),
      make_floatx80_init(0x3ffe, 0xdbfbb797daf25a44ULL),
      make_floatx80_init(0xbffc, 0x901121a0943696f0ULL) },
    { make_floatx80_init(0xbffc, 0xbfffffffffff68daULL),
      make_floatx80_init(0x3ffe, 0xe0ccdeec2a94f811ULL),
      make_floatx80_init(0xbffb, 0xf999089eab583f78ULL) },
    { make_floatx80_init(0xbffc, 0x9fffffffffff4690ULL),
      make_floatx80_init(0x3ffe, 0xe5b906e77c83657eULL),
      make_floatx80_init(0xbffb, 0xd237c8c41be4d410ULL) },
    { make_floatx80_init(0xbffb, 0xffffffffffff8aeeULL),
      make_floatx80_init(0x3ffe, 0xeac0c6e7dd24427cULL),
      make_floatx80_init(0xbffb, 0xa9f9c8c116ddec20ULL) },
    { make_floatx80_init(0xbffb, 0xbfffffffffff2d18ULL),
      make_floatx80_init(0x3ffe, 0xefe4b99bdcdb06ebULL),
      make_floatx80_init(0xbffb, 0x80da33211927c8a8ULL) },
    { make_floatx80_init(0xbffa, 0xffffffffffff8ccbULL),
      make_floatx80_init(0x3ffe, 0xf5257d152486d0f4ULL),
      make_floatx80_init(0xbffa, 0xada82eadb792f0c0ULL) },
    { make_floatx80_init(0xbff9, 0xffffffffffff11feULL),
      make_floatx80_init(0x3ffe, 0xfa83b2db722a0846ULL),
      make_floatx80_init(0xbff9, 0xaf89a491babef740ULL) },
    { floatx80_zero_init,
      make_floatx80_init(0x3fff, 0x8000000000000000ULL),
      floatx80_zero_init },
    { make_floatx80_init(0x3ff9, 0xffffffffffff2680ULL),
      make_floatx80_init(0x3fff, 0x82cd8698ac2b9f6fULL),
      make_floatx80_init(0x3ff9, 0xb361a62b0ae7dbc0ULL) },
    { make_floatx80_init(0x3ffb, 0x800000000000b500ULL),
      make_floatx80_init(0x3fff, 0x85aac367cc488345ULL),
      make_floatx80_init(0x3ffa, 0xb5586cf9891068a0ULL) },
    { make_floatx80_init(0x3ffb, 0xbfffffffffff4b67ULL),
      make_floatx80_init(0x3fff, 0x88980e8092da7cceULL),
      make_floatx80_init(0x3ffb, 0x8980e8092da7cce0ULL) },
    { make_floatx80_init(0x3ffb, 0xffffffffffffff57ULL),
      make_floatx80_init(0x3fff, 0x8b95c1e3ea8bd6dfULL),
      make_floatx80_init(0x3ffb, 0xb95c1e3ea8bd6df0ULL) },
    { make_floatx80_init(0x3ffc, 0x9fffffffffff811fULL),
      make_floatx80_init(0x3fff, 0x8ea4398b45cd4780ULL),
      make_floatx80_init(0x3ffb, 0xea4398b45cd47800ULL) },
    { make_floatx80_init(0x3ffc, 0xbfffffffffff9980ULL),
      make_floatx80_init(0x3fff, 0x91c3d373ab11b919ULL),
      make_floatx80_init(0x3ffc, 0x8e1e9b9d588dc8c8ULL) },
    { make_floatx80_init(0x3ffc, 0xdffffffffffff631ULL),
      make_floatx80_init(0x3fff, 0x94f4efa8fef70864ULL),
      make_floatx80_init(0x3ffc, 0xa7a77d47f7b84320ULL) },
    { make_floatx80_init(0x3ffc, 0xffffffffffff2499ULL),
      make_floatx80_init(0x3fff, 0x9837f0518db892d4ULL),
      make_floatx80_init(0x3ffc, 0xc1bf828c6dc496a0ULL) },
    { make_floatx80_init(0x3ffd, 0x8fffffffffff80fbULL),
      make_floatx80_init(0x3fff, 0x9b8d39b9d54e3a79ULL),
      make_floatx80_init(0x3ffc, 0xdc69cdceaa71d3c8ULL) },
    { make_floatx80_init(0x3ffd, 0x9fffffffffffbc23ULL),
      make_floatx80_init(0x3fff, 0x9ef5326091a10313ULL),
      make_floatx80_init(0x3ffc, 0xf7a993048d081898ULL) },
    { make_floatx80_init(0x3ffd, 0xafffffffffff20ecULL),
      make_floatx80_init(0x3fff, 0xa27043030c49370aULL),
      make_floatx80_init(0x3ffd, 0x89c10c0c3124dc28ULL) },
    { make_floatx80_init(0x3ffd, 0xc00000000000fd2cULL),
      make_floatx80_init(0x3fff, 0xa5fed6a9b15171cfULL),
      make_floatx80_init(0x3ffd, 0x97fb5aa6c545c73cULL) },
    { make_floatx80_init(0x3ffd, 0xd0000000000093beULL),
      make_floatx80_init(0x3fff, 0xa9a15ab4ea7c30e6ULL),
      make_floatx80_init(0x3ffd, 0xa6856ad3a9f0c398ULL) },
    { make_floatx80_init(0x3ffd, 0xe00000000000c2aeULL),
      make_floatx80_init(0x3fff, 0xad583eea42a17876ULL),
      make_floatx80_init(0x3ffd, 0xb560fba90a85e1d8ULL) },
    { make_floatx80_init(0x3ffd, 0xefffffffffff1e3fULL),
      make_floatx80_init(0x3fff, 0xb123f581d2abef6cULL),
      make_floatx80_init(0x3ffd, 0xc48fd6074aafbdb0ULL) },
    { make_floatx80_init(0x3ffd, 0xffffffffffff1c23ULL),
      make_floatx80_init(0x3fff, 0xb504f333f9de2cadULL),
      make_floatx80_init(0x3ffd, 0xd413cccfe778b2b4ULL) },
    { make_floatx80_init(0x3ffe, 0x8800000000006344ULL),
      make_floatx80_init(0x3fff, 0xb8fbaf4762fbd0a1ULL),
      make_floatx80_init(0x3ffd, 0xe3eebd1d8bef4284ULL) },
    { make_floatx80_init(0x3ffe, 0x9000000000005d67ULL),
      make_floatx80_init(0x3fff, 0xbd08a39f580c668dULL),
      make_floatx80_init(0x3ffd, 0xf4228e7d60319a34ULL) },
    { make_floatx80_init(0x3ffe, 0x9800000000009127ULL),
      make_floatx80_init(0x3fff, 0xc12c4cca6670e042ULL),
      make_floatx80_init(0x3ffe, 0x82589994cce1c084ULL) },
    { make_floatx80_init(0x3ffe, 0x9fffffffffff06f9ULL),
      make_floatx80_init(0x3fff, 0xc5672a11550655c3ULL),
      make_floatx80_init(0x3ffe, 0x8ace5422aa0cab86ULL) },
    { make_floatx80_init(0x3ffe, 0xa7fffffffffff80dULL),
      make_floatx80_init(0x3fff, 0xc9b9bd866e2f234bULL),
      make_floatx80_init(0x3ffe, 0x93737b0cdc5e4696ULL) },
    { make_floatx80_init(0x3ffe, 0xafffffffffff1470ULL),
      make_floatx80_init(0x3fff, 0xce248c151f83fd69ULL),
      make_floatx80_init(0x3ffe, 0x9c49182a3f07fad2ULL) },
    { make_floatx80_init(0x3ffe, 0xb800000000000e0aULL),
      make_floatx80_init(0x3fff, 0xd2a81d91f12aec5cULL),
      make_floatx80_init(0x3ffe, 0xa5503b23e255d8b8ULL) },
    { make_floatx80_init(0x3ffe, 0xc00000000000b7faULL),
      make_floatx80_init(0x3fff, 0xd744fccad69dd630ULL),
      make_floatx80_init(0x3ffe, 0xae89f995ad3bac60ULL) },
    { make_floatx80_init(0x3ffe, 0xc800000000003aa6ULL),
      make_floatx80_init(0x3fff, 0xdbfbb797daf25a44ULL),
      make_floatx80_init(0x3ffe, 0xb7f76f2fb5e4b488ULL) },
    { make_floatx80_init(0x3ffe, 0xd00000000000a6aeULL),
      make_floatx80_init(0x3fff, 0xe0ccdeec2a954685ULL),
      make_floatx80_init(0x3ffe, 0xc199bdd8552a8d0aULL) },
    { make_floatx80_init(0x3ffe, 0xd800000000004165ULL),
      make_floatx80_init(0x3fff, 0xe5b906e77c837155ULL),
      make_floatx80_init(0x3ffe, 0xcb720dcef906e2aaULL) },
    { make_floatx80_init(0x3ffe, 0xe00000000000582cULL),
      make_floatx80_init(0x3fff, 0xeac0c6e7dd24713aULL),
      make_floatx80_init(0x3ffe, 0xd5818dcfba48e274ULL) },
    { make_floatx80_init(0x3ffe, 0xe800000000001a5dULL),
      make_floatx80_init(0x3fff, 0xefe4b99bdcdb06ebULL),
      make_floatx80_init(0x3ffe, 0xdfc97337b9b60dd6ULL) },
    { make_floatx80_init(0x3ffe, 0xefffffffffffc1efULL),
      make_floatx80_init(0x3fff, 0xf5257d152486a2faULL),
      make_floatx80_init(0x3ffe, 0xea4afa2a490d45f4ULL) },
    { make_floatx80_init(0x3ffe, 0xf800000000001069ULL),
      make_floatx80_init(0x3fff, 0xfa83b2db722a0e5cULL),
      make_floatx80_init(0x3ffe, 0xf50765b6e4541cb8ULL) },
    { make_floatx80_init(0x3fff, 0x8000000000000000ULL),
      make_floatx80_init(0x4000, 0x8000000000000000ULL),
      make_floatx80_init(0x3fff, 0x8000000000000000ULL) },
};

void helper_f2xm1(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t sig = extractFloatx80Frac(ST0);
    int32_t exp = extractFloatx80Exp(ST0);
    bool sign = extractFloatx80Sign(ST0);

    if (floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
    } else if (exp > 0x3fff ||
               (exp == 0x3fff && sig != (0x8000000000000000ULL))) {
        /* Out of range for the instruction, treat as invalid.  */
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (exp == 0x3fff) {
        /* Argument 1 or -1, exact result 1 or -0.5.  */
        if (sign) {
            ST0 = make_floatx80(0xbffe, 0x8000000000000000ULL);
        }
    } else if (exp < 0x3fb0) {
        if (!floatx80_is_zero(ST0)) {
            /*
             * Multiplying the argument by an extra-precision version
             * of log(2) is sufficiently precise.  Zero arguments are
             * returned unchanged.
             */
            uint64_t sig0, sig1, sig2;
            if (exp == 0) {
                normalizeFloatx80Subnormal(sig, &exp, &sig);
            }
            mul128By64To192(ln2_sig_high, ln2_sig_low, sig, &sig0, &sig1,
                            &sig2);
            /* This result is inexact.  */
            sig1 |= 1;
            ST0 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                sign, exp, sig0, sig1,
                                                &env->fp_status);
        }
    } else {
        floatx80 tmp, y, accum;
        bool asign, bsign;
        int32_t n, aexp, bexp;
        uint64_t asig0, asig1, asig2, bsig0, bsig1;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        /* Find the nearest multiple of 1/32 to the argument.  */
        tmp = floatx80_scalbn(ST0, 5, &env->fp_status);
        n = 32 + floatx80_to_int32(tmp, &env->fp_status);
        y = floatx80_sub(ST0, f2xm1_table[n].t, &env->fp_status);

        if (floatx80_is_zero(y)) {
            /*
             * Use the value of 2^t - 1 from the table, to avoid
             * needing to special-case zero as a result of
             * multiplication below.
             */
            ST0 = f2xm1_table[n].t;
            set_float_exception_flags(float_flag_inexact, &env->fp_status);
            env->fp_status.float_rounding_mode = save_mode;
        } else {
            /*
             * Compute the lower parts of a polynomial expansion for
             * (2^y - 1) / y.
             */
            accum = floatx80_mul(f2xm1_coeff_7, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_6, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_5, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_4, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_3, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_2, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_1, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_0_low, accum, &env->fp_status);

            /*
             * The full polynomial expansion is f2xm1_coeff_0 + accum
             * (where accum has much lower magnitude, and so, in
             * particular, carry out of the addition is not possible).
             * (This expansion is only accurate to about 70 bits, not
             * 128 bits.)
             */
            aexp = extractFloatx80Exp(f2xm1_coeff_0);
            asign = extractFloatx80Sign(f2xm1_coeff_0);
            shift128RightJamming(extractFloatx80Frac(accum), 0,
                                 aexp - extractFloatx80Exp(accum),
                                 &asig0, &asig1);
            bsig0 = extractFloatx80Frac(f2xm1_coeff_0);
            bsig1 = 0;
            if (asign == extractFloatx80Sign(accum)) {
                add128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
            } else {
                sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
            }
            /* And thus compute an approximation to 2^y - 1.  */
            mul128By64To192(asig0, asig1, extractFloatx80Frac(y),
                            &asig0, &asig1, &asig2);
            aexp += extractFloatx80Exp(y) - 0x3ffe;
            asign ^= extractFloatx80Sign(y);
            if (n != 32) {
                /*
                 * Multiply this by the precomputed value of 2^t and
                 * add that of 2^t - 1.
                 */
                mul128By64To192(asig0, asig1,
                                extractFloatx80Frac(f2xm1_table[n].exp2),
                                &asig0, &asig1, &asig2);
                aexp += extractFloatx80Exp(f2xm1_table[n].exp2) - 0x3ffe;
                bexp = extractFloatx80Exp(f2xm1_table[n].exp2m1);
                bsig0 = extractFloatx80Frac(f2xm1_table[n].exp2m1);
                bsig1 = 0;
                if (bexp < aexp) {
                    shift128RightJamming(bsig0, bsig1, aexp - bexp,
                                         &bsig0, &bsig1);
                } else if (aexp < bexp) {
                    shift128RightJamming(asig0, asig1, bexp - aexp,
                                         &asig0, &asig1);
                    aexp = bexp;
                }
                /* The sign of 2^t - 1 is always that of the result.  */
                bsign = extractFloatx80Sign(f2xm1_table[n].exp2m1);
                if (asign == bsign) {
                    /* Avoid possible carry out of the addition.  */
                    shift128RightJamming(asig0, asig1, 1,
                                         &asig0, &asig1);
                    shift128RightJamming(bsig0, bsig1, 1,
                                         &bsig0, &bsig1);
                    ++aexp;
                    add128(asig0, asig1, bsig0, bsig1, &asig0, &asig1);
                } else {
                    sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
                    asign = bsign;
                }
            }
            env->fp_status.float_rounding_mode = save_mode;
            /* This result is inexact.  */
            asig1 |= 1;
            ST0 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                asign, aexp, asig0, asig1,
                                                &env->fp_status);
        }

        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fptan(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        fptemp = tan(fptemp);
        ST0 = double_to_floatx80(env, fptemp);
        fpush(env);
        ST0 = floatx80_one;
        env->fpus &= ~0x400; /* C2 <-- 0 */
        /* the above code is for |arg| < 2**52 only */
    }
}

/* Values of pi/4, pi/2, 3pi/4 and pi, with 128-bit precision.  */
#define pi_4_exp 0x3ffe
#define pi_4_sig_high 0xc90fdaa22168c234ULL
#define pi_4_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_2_exp 0x3fff
#define pi_2_sig_high 0xc90fdaa22168c234ULL
#define pi_2_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_34_exp 0x4000
#define pi_34_sig_high 0x96cbe3f9990e91a7ULL
#define pi_34_sig_low 0x9394c9e8a0a5159dULL
#define pi_exp 0x4000
#define pi_sig_high 0xc90fdaa22168c234ULL
#define pi_sig_low 0xc4c6628b80dc1cd1ULL

/*
 * Polynomial coefficients for an approximation to atan(x), with only
 * odd powers of x used, for x in the interval [-1/16, 1/16].  (Unlike
 * for some other approximations, no low part is needed for the first
 * coefficient here to achieve a sufficiently accurate result, because
 * the coefficient in this minimax approximation is very close to
 * exactly 1.)
 */
#define fpatan_coeff_0 make_floatx80(0x3fff, 0x8000000000000000ULL)
#define fpatan_coeff_1 make_floatx80(0xbffd, 0xaaaaaaaaaaaaaa43ULL)
#define fpatan_coeff_2 make_floatx80(0x3ffc, 0xccccccccccbfe4f8ULL)
#define fpatan_coeff_3 make_floatx80(0xbffc, 0x92492491fbab2e66ULL)
#define fpatan_coeff_4 make_floatx80(0x3ffb, 0xe38e372881ea1e0bULL)
#define fpatan_coeff_5 make_floatx80(0xbffb, 0xba2c0104bbdd0615ULL)
#define fpatan_coeff_6 make_floatx80(0x3ffb, 0x9baf7ebf898b42efULL)

struct fpatan_data {
    /* High and low parts of atan(x).  */
    floatx80 atan_high, atan_low;
};

static const struct fpatan_data fpatan_table[9] = {
    { floatx80_zero_init,
      floatx80_zero_init },
    { make_floatx80_init(0x3ffb, 0xfeadd4d5617b6e33ULL),
      make_floatx80_init(0xbfb9, 0xdda19d8305ddc420ULL) },
    { make_floatx80_init(0x3ffc, 0xfadbafc96406eb15ULL),
      make_floatx80_init(0x3fbb, 0xdb8f3debef442fccULL) },
    { make_floatx80_init(0x3ffd, 0xb7b0ca0f26f78474ULL),
      make_floatx80_init(0xbfbc, 0xeab9bdba460376faULL) },
    { make_floatx80_init(0x3ffd, 0xed63382b0dda7b45ULL),
      make_floatx80_init(0x3fbc, 0xdfc88bd978751a06ULL) },
    { make_floatx80_init(0x3ffe, 0x8f005d5ef7f59f9bULL),
      make_floatx80_init(0x3fbd, 0xb906bc2ccb886e90ULL) },
    { make_floatx80_init(0x3ffe, 0xa4bc7d1934f70924ULL),
      make_floatx80_init(0x3fbb, 0xcd43f9522bed64f8ULL) },
    { make_floatx80_init(0x3ffe, 0xb8053e2bc2319e74ULL),
      make_floatx80_init(0xbfbc, 0xd3496ab7bd6eef0cULL) },
    { make_floatx80_init(0x3ffe, 0xc90fdaa22168c235ULL),
      make_floatx80_init(0xbfbc, 0xece675d1fc8f8cbcULL) },
};

void helper_fpatan(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_invalid_encoding(ST0) ||
               floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (floatx80_is_zero(ST1) && !arg0_sign) {
        /* Pass this zero through.  */
    } else if (((floatx80_is_infinity(ST0) && !floatx80_is_infinity(ST1)) ||
                 arg0_exp - arg1_exp >= 80) &&
               !arg0_sign) {
        /*
         * Dividing ST1 by ST0 gives the correct result up to
         * rounding, and avoids spurious underflow exceptions that
         * might result from passing some small values through the
         * polynomial approximation, but if a finite nonzero result of
         * division is exact, the result of fpatan is still inexact
         * (and underflowing where appropriate).
         */
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;
        ST1 = floatx80_div(ST1, ST0, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save_prec;
        if (!floatx80_is_zero(ST1) &&
            !(get_float_exception_flags(&env->fp_status) &
              float_flag_inexact)) {
            /*
             * The mathematical result is very slightly closer to zero
             * than this exact result.  Round a value with the
             * significand adjusted accordingly to get the correct
             * exceptions, and possibly an adjusted result depending
             * on the rounding mode.
             */
            uint64_t sig = extractFloatx80Frac(ST1);
            int32_t exp = extractFloatx80Exp(ST1);
            bool sign = extractFloatx80Sign(ST1);
            if (exp == 0) {
                normalizeFloatx80Subnormal(sig, &exp, &sig);
            }
            ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                sign, exp, sig - 1,
                                                -1, &env->fp_status);
        }
    } else {
        /* The result is inexact.  */
        bool rsign = arg1_sign;
        int32_t rexp;
        uint64_t rsig0, rsig1;
        if (floatx80_is_zero(ST1)) {
            /*
             * ST0 is negative.  The result is pi with the sign of
             * ST1.
             */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else if (floatx80_is_infinity(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                if (arg0_sign) {
                    rexp = pi_34_exp;
                    rsig0 = pi_34_sig_high;
                    rsig1 = pi_34_sig_low;
                } else {
                    rexp = pi_4_exp;
                    rsig0 = pi_4_sig_high;
                    rsig1 = pi_4_sig_low;
                }
            } else {
                rexp = pi_2_exp;
                rsig0 = pi_2_sig_high;
                rsig1 = pi_2_sig_low;
            }
        } else if (floatx80_is_zero(ST0) || arg1_exp - arg0_exp >= 80) {
            rexp = pi_2_exp;
            rsig0 = pi_2_sig_high;
            rsig1 = pi_2_sig_low;
        } else if (floatx80_is_infinity(ST0) || arg0_exp - arg1_exp >= 80) {
            /* ST0 is negative.  */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else {
            /*
             * ST0 and ST1 are finite, nonzero and with exponents not
             * too far apart.
             */
            int32_t adj_exp, num_exp, den_exp, xexp, yexp, n, texp, zexp, aexp;
            int32_t azexp, axexp;
            bool adj_sub, ysign, zsign;
            uint64_t adj_sig0, adj_sig1, num_sig, den_sig, xsig0, xsig1;
            uint64_t msig0, msig1, msig2, remsig0, remsig1, remsig2;
            uint64_t ysig0, ysig1, tsig, zsig0, zsig1, asig0, asig1;
            uint64_t azsig0, azsig1;
            uint64_t azsig2, azsig3, axsig0, axsig1;
            floatx80 x8;
            FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
            FloatX80RoundPrec save_prec =
                env->fp_status.floatx80_rounding_precision;
            env->fp_status.float_rounding_mode = float_round_nearest_even;
            env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

            if (arg0_exp == 0) {
                normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
            }
            if (arg1_exp == 0) {
                normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
            }
            if (arg0_exp > arg1_exp ||
                (arg0_exp == arg1_exp && arg0_sig >= arg1_sig)) {
                /* Work with abs(ST1) / abs(ST0).  */
                num_exp = arg1_exp;
                num_sig = arg1_sig;
                den_exp = arg0_exp;
                den_sig = arg0_sig;
                if (arg0_sign) {
                    /* The result is subtracted from pi.  */
                    adj_exp = pi_exp;
                    adj_sig0 = pi_sig_high;
                    adj_sig1 = pi_sig_low;
                    adj_sub = true;
                } else {
                    /* The result is used as-is.  */
                    adj_exp = 0;
                    adj_sig0 = 0;
                    adj_sig1 = 0;
                    adj_sub = false;
                }
            } else {
                /* Work with abs(ST0) / abs(ST1).  */
                num_exp = arg0_exp;
                num_sig = arg0_sig;
                den_exp = arg1_exp;
                den_sig = arg1_sig;
                /* The result is added to or subtracted from pi/2.  */
                adj_exp = pi_2_exp;
                adj_sig0 = pi_2_sig_high;
                adj_sig1 = pi_2_sig_low;
                adj_sub = !arg0_sign;
            }

            /*
             * Compute x = num/den, where 0 < x <= 1 and x is not too
             * small.
             */
            xexp = num_exp - den_exp + 0x3ffe;
            remsig0 = num_sig;
            remsig1 = 0;
            if (den_sig <= remsig0) {
                shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                ++xexp;
            }
            xsig0 = estimateDiv128To64(remsig0, remsig1, den_sig);
            mul64To128(den_sig, xsig0, &msig0, &msig1);
            sub128(remsig0, remsig1, msig0, msig1, &remsig0, &remsig1);
            while ((int64_t) remsig0 < 0) {
                --xsig0;
                add128(remsig0, remsig1, 0, den_sig, &remsig0, &remsig1);
            }
            xsig1 = estimateDiv128To64(remsig1, 0, den_sig);
            /*
             * No need to correct any estimation error in xsig1; even
             * with such error, it is accurate enough.
             */

            /*
             * Split x as x = t + y, where t = n/8 is the nearest
             * multiple of 1/8 to x.
             */
            x8 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                               false, xexp + 3, xsig0,
                                               xsig1, &env->fp_status);
            n = floatx80_to_int32(x8, &env->fp_status);
            if (n == 0) {
                ysign = false;
                yexp = xexp;
                ysig0 = xsig0;
                ysig1 = xsig1;
                texp = 0;
                tsig = 0;
            } else {
                int shift = clz32(n) + 32;
                texp = 0x403b - shift;
                tsig = n;
                tsig <<= shift;
                if (texp == xexp) {
                    sub128(xsig0, xsig1, tsig, 0, &ysig0, &ysig1);
                    if ((int64_t) ysig0 >= 0) {
                        ysign = false;
                        if (ysig0 == 0) {
                            if (ysig1 == 0) {
                                yexp = 0;
                            } else {
                                shift = clz64(ysig1) + 64;
                                yexp = xexp - shift;
                                shift128Left(ysig0, ysig1, shift,
                                             &ysig0, &ysig1);
                            }
                        } else {
                            shift = clz64(ysig0);
                            yexp = xexp - shift;
                            shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                        }
                    } else {
                        ysign = true;
                        sub128(0, 0, ysig0, ysig1, &ysig0, &ysig1);
                        if (ysig0 == 0) {
                            shift = clz64(ysig1) + 64;
                        } else {
                            shift = clz64(ysig0);
                        }
                        yexp = xexp - shift;
                        shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                    }
                } else {
                    /*
                     * t's exponent must be greater than x's because t
                     * is positive and the nearest multiple of 1/8 to
                     * x, and if x has a greater exponent, the power
                     * of 2 with that exponent is also a multiple of
                     * 1/8.
                     */
                    uint64_t usig0, usig1;
                    shift128RightJamming(xsig0, xsig1, texp - xexp,
                                         &usig0, &usig1);
                    ysign = true;
                    sub128(tsig, 0, usig0, usig1, &ysig0, &ysig1);
                    if (ysig0 == 0) {
                        shift = clz64(ysig1) + 64;
                    } else {
                        shift = clz64(ysig0);
                    }
                    yexp = texp - shift;
                    shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                }
            }

            /*
             * Compute z = y/(1+tx), so arctan(x) = arctan(t) +
             * arctan(z).
             */
            zsign = ysign;
            if (texp == 0 || yexp == 0) {
                zexp = yexp;
                zsig0 = ysig0;
                zsig1 = ysig1;
            } else {
                /*
                 * t <= 1, x <= 1 and if both are 1 then y is 0, so tx < 1.
                 */
                int32_t dexp = texp + xexp - 0x3ffe;
                uint64_t dsig0, dsig1, dsig2;
                mul128By64To192(xsig0, xsig1, tsig, &dsig0, &dsig1, &dsig2);
                /*
                 * dexp <= 0x3fff (and if equal, dsig0 has a leading 0
                 * bit).  Add 1 to produce the denominator 1+tx.
                 */
                shift128RightJamming(dsig0, dsig1, 0x3fff - dexp,
                                     &dsig0, &dsig1);
                dsig0 |= 0x8000000000000000ULL;
                zexp = yexp - 1;
                remsig0 = ysig0;
                remsig1 = ysig1;
                remsig2 = 0;
                if (dsig0 <= remsig0) {
                    shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                    ++zexp;
                }
                zsig0 = estimateDiv128To64(remsig0, remsig1, dsig0);
                mul128By64To192(dsig0, dsig1, zsig0, &msig0, &msig1, &msig2);
                sub192(remsig0, remsig1, remsig2, msig0, msig1, msig2,
                       &remsig0, &remsig1, &remsig2);
                while ((int64_t) remsig0 < 0) {
                    --zsig0;
                    add192(remsig0, remsig1, remsig2, 0, dsig0, dsig1,
                           &remsig0, &remsig1, &remsig2);
                }
                zsig1 = estimateDiv128To64(remsig1, remsig2, dsig0);
                /* No need to correct any estimation error in zsig1.  */
            }

            if (zexp == 0) {
                azexp = 0;
                azsig0 = 0;
                azsig1 = 0;
            } else {
                floatx80 z2, accum;
                uint64_t z2sig0, z2sig1, z2sig2, z2sig3;
                /* Compute z^2.  */
                mul128To256(zsig0, zsig1, zsig0, zsig1,
                            &z2sig0, &z2sig1, &z2sig2, &z2sig3);
                z2 = normalizeRoundAndPackFloatx80(floatx80_precision_x, false,
                                                   zexp + zexp - 0x3ffe,
                                                   z2sig0, z2sig1,
                                                   &env->fp_status);

                /* Compute the lower parts of the polynomial expansion.  */
                accum = floatx80_mul(fpatan_coeff_6, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_5, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_4, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_3, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_2, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_1, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);

                /*
                 * The full polynomial expansion is z*(fpatan_coeff_0 + accum).
                 * fpatan_coeff_0 is 1, and accum is negative and much smaller.
                 */
                aexp = extractFloatx80Exp(fpatan_coeff_0);
                shift128RightJamming(extractFloatx80Frac(accum), 0,
                                     aexp - extractFloatx80Exp(accum),
                                     &asig0, &asig1);
                sub128(extractFloatx80Frac(fpatan_coeff_0), 0, asig0, asig1,
                       &asig0, &asig1);
                /* Multiply by z to compute arctan(z).  */
                azexp = aexp + zexp - 0x3ffe;
                mul128To256(asig0, asig1, zsig0, zsig1, &azsig0, &azsig1,
                            &azsig2, &azsig3);
            }

            /* Add arctan(t) (positive or zero) and arctan(z) (sign zsign).  */
            if (texp == 0) {
                /* z is positive.  */
                axexp = azexp;
                axsig0 = azsig0;
                axsig1 = azsig1;
            } else {
                bool low_sign = extractFloatx80Sign(fpatan_table[n].atan_low);
                int32_t low_exp = extractFloatx80Exp(fpatan_table[n].atan_low);
                uint64_t low_sig0 =
                    extractFloatx80Frac(fpatan_table[n].atan_low);
                uint64_t low_sig1 = 0;
                axexp = extractFloatx80Exp(fpatan_table[n].atan_high);
                axsig0 = extractFloatx80Frac(fpatan_table[n].atan_high);
                axsig1 = 0;
                shift128RightJamming(low_sig0, low_sig1, axexp - low_exp,
                                     &low_sig0, &low_sig1);
                if (low_sign) {
                    sub128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                }
                if (azexp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, azexp - axexp + 1,
                                         &axsig0, &axsig1);
                    axexp = azexp + 1;
                    shift128RightJamming(azsig0, azsig1, 1,
                                         &azsig0, &azsig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(azsig0, azsig1, axexp - azexp + 1,
                                         &azsig0, &azsig1);
                    ++axexp;
                }
                if (zsign) {
                    sub128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                }
            }

            if (adj_exp == 0) {
                rexp = axexp;
                rsig0 = axsig0;
                rsig1 = axsig1;
            } else {
                /*
                 * Add or subtract arctan(x) (exponent axexp,
                 * significand axsig0 and axsig1, positive, not
                 * necessarily normalized) to the number given by
                 * adj_exp, adj_sig0 and adj_sig1, according to
                 * adj_sub.
                 */
                if (adj_exp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, adj_exp - axexp + 1,
                                         &axsig0, &axsig1);
                    rexp = adj_exp + 1;
                    shift128RightJamming(adj_sig0, adj_sig1, 1,
                                         &adj_sig0, &adj_sig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(adj_sig0, adj_sig1,
                                         axexp - adj_exp + 1,
                                         &adj_sig0, &adj_sig1);
                    rexp = axexp + 1;
                }
                if (adj_sub) {
                    sub128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                } else {
                    add128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                }
            }

            env->fp_status.float_rounding_mode = save_mode;
            env->fp_status.floatx80_rounding_precision = save_prec;
        }
        /* This result is inexact.  */
        rsig1 |= 1;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x, rsign, rexp,
                                            rsig0, rsig1, &env->fp_status);
    }

    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fxtract(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    CPU_LDoubleU temp;

    temp.d = ST0;

    if (floatx80_is_zero(ST0)) {
        /* Easy way to generate -inf and raising division by 0 exception */
        ST0 = floatx80_div(floatx80_chs(floatx80_one), floatx80_zero,
                           &env->fp_status);
        fpush(env);
        ST0 = temp.d;
    } else if (floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
        fpush(env);
        ST0 = ST1;
    } else if (floatx80_is_any_nan(ST0)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
        fpush(env);
        ST0 = ST1;
    } else if (floatx80_is_infinity(ST0)) {
        fpush(env);
        ST0 = ST1;
        ST1 = floatx80_infinity;
    } else {
        int expdif;

        if (EXPD(temp) == 0) {
            int shift = clz64(temp.l.lower);
            temp.l.lower <<= shift;
            expdif = 1 - EXPBIAS - shift;
            float_raise(float_flag_input_denormal, &env->fp_status);
        } else {
            expdif = EXPD(temp) - EXPBIAS;
        }
        /* DP exponent bias */
        ST0 = int32_to_floatx80(expdif, &env->fp_status);
        fpush(env);
        BIASEXPONENT(temp);
        ST0 = temp.d;
    }
    merge_exception_flags(env, old_flags);
}

static void helper_fprem_common(CPUX86State *env, bool mod)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t quotient;
    CPU_LDoubleU temp0, temp1;
    int exp0, exp1, expdiff;

    temp0.d = ST0;
    temp1.d = ST1;
    exp0 = EXPD(temp0);
    exp1 = EXPD(temp1);

    env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
    if (floatx80_is_zero(ST0) || floatx80_is_zero(ST1) ||
        exp0 == 0x7fff || exp1 == 0x7fff ||
        floatx80_invalid_encoding(ST0) || floatx80_invalid_encoding(ST1)) {
        ST0 = floatx80_modrem(ST0, ST1, mod, &quotient, &env->fp_status);
    } else {
        if (exp0 == 0) {
            exp0 = 1 - clz64(temp0.l.lower);
        }
        if (exp1 == 0) {
            exp1 = 1 - clz64(temp1.l.lower);
        }
        expdiff = exp0 - exp1;
        if (expdiff < 64) {
            ST0 = floatx80_modrem(ST0, ST1, mod, &quotient, &env->fp_status);
            env->fpus |= (quotient & 0x4) << (8 - 2);  /* (C0) <-- q2 */
            env->fpus |= (quotient & 0x2) << (14 - 1); /* (C3) <-- q1 */
            env->fpus |= (quotient & 0x1) << (9 - 0);  /* (C1) <-- q0 */
        } else {
            /*
             * Partial remainder.  This choice of how many bits to
             * process at once is specified in AMD instruction set
             * manuals, and empirically is followed by Intel
             * processors as well; it ensures that the final remainder
             * operation in a loop does produce the correct low three
             * bits of the quotient.  AMD manuals specify that the
             * flags other than C2 are cleared, and empirically Intel
             * processors clear them as well.
             */
            int n = 32 + (expdiff % 32);
            temp1.d = floatx80_scalbn(temp1.d, expdiff - n, &env->fp_status);
            ST0 = floatx80_mod(ST0, temp1.d, &env->fp_status);
            env->fpus |= 0x400;  /* C2 <-- 1 */
        }
    }
    merge_exception_flags(env, old_flags);
}

void helper_fprem1(CPUX86State *env)
{
    helper_fprem_common(env, false);
}

void helper_fprem(CPUX86State *env)
{
    helper_fprem_common(env, true);
}

/* 128-bit significand of log2(e).  */
#define log2_e_sig_high 0xb8aa3b295c17f0bbULL
#define log2_e_sig_low 0xbe87fed0691d3e89ULL

/*
 * Polynomial coefficients for an approximation to log2((1+x)/(1-x)),
 * with only odd powers of x used, for x in the interval [2*sqrt(2)-3,
 * 3-2*sqrt(2)], which corresponds to logarithms of numbers in the
 * interval [sqrt(2)/2, sqrt(2)].
 */
#define fyl2x_coeff_0 make_floatx80(0x4000, 0xb8aa3b295c17f0bcULL)
#define fyl2x_coeff_0_low make_floatx80(0xbfbf, 0x834972fe2d7bab1bULL)
#define fyl2x_coeff_1 make_floatx80(0x3ffe, 0xf6384ee1d01febb8ULL)
#define fyl2x_coeff_2 make_floatx80(0x3ffe, 0x93bb62877cdfa2e3ULL)
#define fyl2x_coeff_3 make_floatx80(0x3ffd, 0xd30bb153d808f269ULL)
#define fyl2x_coeff_4 make_floatx80(0x3ffd, 0xa42589eaf451499eULL)
#define fyl2x_coeff_5 make_floatx80(0x3ffd, 0x864d42c0f8f17517ULL)
#define fyl2x_coeff_6 make_floatx80(0x3ffc, 0xe3476578adf26272ULL)
#define fyl2x_coeff_7 make_floatx80(0x3ffc, 0xc506c5f874e6d80fULL)
#define fyl2x_coeff_8 make_floatx80(0x3ffc, 0xac5cf50cc57d6372ULL)
#define fyl2x_coeff_9 make_floatx80(0x3ffc, 0xb1ed0066d971a103ULL)

/*
 * Compute an approximation of log2(1+arg), where 1+arg is in the
 * interval [sqrt(2)/2, sqrt(2)].  It is assumed that when this
 * function is called, rounding precision is set to 80 and the
 * round-to-nearest mode is in effect.  arg must not be exactly zero,
 * and must not be so close to zero that underflow might occur.
 */
static void helper_fyl2x_common(CPUX86State *env, floatx80 arg, int32_t *exp,
                                uint64_t *sig0, uint64_t *sig1)
{
    uint64_t arg0_sig = extractFloatx80Frac(arg);
    int32_t arg0_exp = extractFloatx80Exp(arg);
    bool arg0_sign = extractFloatx80Sign(arg);
    bool asign;
    int32_t dexp, texp, aexp;
    uint64_t dsig0, dsig1, tsig0, tsig1, rsig0, rsig1, rsig2;
    uint64_t msig0, msig1, msig2, t2sig0, t2sig1, t2sig2, t2sig3;
    uint64_t asig0, asig1, asig2, asig3, bsig0, bsig1;
    floatx80 t2, accum;

    /*
     * Compute an approximation of arg/(2+arg), with extra precision,
     * as the argument to a polynomial approximation.  The extra
     * precision is only needed for the first term of the
     * approximation, with subsequent terms being significantly
     * smaller; the approximation only uses odd exponents, and the
     * square of arg/(2+arg) is at most 17-12*sqrt(2) = 0.029....
     */
    if (arg0_sign) {
        dexp = 0x3fff;
        shift128RightJamming(arg0_sig, 0, dexp - arg0_exp, &dsig0, &dsig1);
        sub128(0, 0, dsig0, dsig1, &dsig0, &dsig1);
    } else {
        dexp = 0x4000;
        shift128RightJamming(arg0_sig, 0, dexp - arg0_exp, &dsig0, &dsig1);
        dsig0 |= 0x8000000000000000ULL;
    }
    texp = arg0_exp - dexp + 0x3ffe;
    rsig0 = arg0_sig;
    rsig1 = 0;
    rsig2 = 0;
    if (dsig0 <= rsig0) {
        shift128Right(rsig0, rsig1, 1, &rsig0, &rsig1);
        ++texp;
    }
    tsig0 = estimateDiv128To64(rsig0, rsig1, dsig0);
    mul128By64To192(dsig0, dsig1, tsig0, &msig0, &msig1, &msig2);
    sub192(rsig0, rsig1, rsig2, msig0, msig1, msig2,
           &rsig0, &rsig1, &rsig2);
    while ((int64_t) rsig0 < 0) {
        --tsig0;
        add192(rsig0, rsig1, rsig2, 0, dsig0, dsig1,
               &rsig0, &rsig1, &rsig2);
    }
    tsig1 = estimateDiv128To64(rsig1, rsig2, dsig0);
    /*
     * No need to correct any estimation error in tsig1; even with
     * such error, it is accurate enough.  Now compute the square of
     * that approximation.
     */
    mul128To256(tsig0, tsig1, tsig0, tsig1,
                &t2sig0, &t2sig1, &t2sig2, &t2sig3);
    t2 = normalizeRoundAndPackFloatx80(floatx80_precision_x, false,
                                       texp + texp - 0x3ffe,
                                       t2sig0, t2sig1, &env->fp_status);

    /* Compute the lower parts of the polynomial expansion.  */
    accum = floatx80_mul(fyl2x_coeff_9, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_8, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_7, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_6, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_5, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_4, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_3, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_2, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_1, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_0_low, accum, &env->fp_status);

    /*
     * The full polynomial expansion is fyl2x_coeff_0 + accum (where
     * accum has much lower magnitude, and so, in particular, carry
     * out of the addition is not possible), multiplied by t.  (This
     * expansion is only accurate to about 70 bits, not 128 bits.)
     */
    aexp = extractFloatx80Exp(fyl2x_coeff_0);
    asign = extractFloatx80Sign(fyl2x_coeff_0);
    shift128RightJamming(extractFloatx80Frac(accum), 0,
                         aexp - extractFloatx80Exp(accum),
                         &asig0, &asig1);
    bsig0 = extractFloatx80Frac(fyl2x_coeff_0);
    bsig1 = 0;
    if (asign == extractFloatx80Sign(accum)) {
        add128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
    } else {
        sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
    }
    /* Multiply by t to compute the required result.  */
    mul128To256(asig0, asig1, tsig0, tsig1,
                &asig0, &asig1, &asig2, &asig3);
    aexp += texp - 0x3ffe;
    *exp = aexp;
    *sig0 = asig0;
    *sig1 = asig1;
}

void helper_fyl2xp1(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_invalid_encoding(ST0) ||
               floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (arg0_exp > 0x3ffd ||
               (arg0_exp == 0x3ffd && arg0_sig > (arg0_sign ?
                                                  0x95f619980c4336f7ULL :
                                                  0xd413cccfe7799211ULL))) {
        /*
         * Out of range for the instruction (ST0 must have absolute
         * value less than 1 - sqrt(2)/2 = 0.292..., according to
         * Intel manuals; AMD manuals allow a range from sqrt(2)/2 - 1
         * to sqrt(2) - 1, which we allow here), treat as invalid.
         */
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_zero(ST0) || floatx80_is_zero(ST1) ||
               arg1_exp == 0x7fff) {
        /*
         * One argument is zero, or multiplying by infinity; correct
         * result is exact and can be obtained by multiplying the
         * arguments.
         */
        ST1 = floatx80_mul(ST0, ST1, &env->fp_status);
    } else if (arg0_exp < 0x3fb0) {
        /*
         * Multiplying both arguments and an extra-precision version
         * of log2(e) is sufficiently precise.
         */
        uint64_t sig0, sig1, sig2;
        int32_t exp;
        if (arg0_exp == 0) {
            normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
        }
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        mul128By64To192(log2_e_sig_high, log2_e_sig_low, arg0_sig,
                        &sig0, &sig1, &sig2);
        exp = arg0_exp + 1;
        mul128By64To192(sig0, sig1, arg1_sig, &sig0, &sig1, &sig2);
        exp += arg1_exp - 0x3ffe;
        /* This result is inexact.  */
        sig1 |= 1;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                            arg0_sign ^ arg1_sign, exp,
                                            sig0, sig1, &env->fp_status);
    } else {
        int32_t aexp;
        uint64_t asig0, asig1, asig2;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        helper_fyl2x_common(env, ST0, &aexp, &asig0, &asig1);
        /*
         * Multiply by the second argument to compute the required
         * result.
         */
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        mul128By64To192(asig0, asig1, arg1_sig, &asig0, &asig1, &asig2);
        aexp += arg1_exp - 0x3ffe;
        /* This result is inexact.  */
        asig1 |= 1;
        env->fp_status.float_rounding_mode = save_mode;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                            arg0_sign ^ arg1_sign, aexp,
                                            asig0, asig1, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fyl2x(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_invalid_encoding(ST0) ||
               floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (arg0_sign && !floatx80_is_zero(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_infinity(ST1)) {
        FloatRelation cmp = floatx80_compare(ST0, floatx80_one,
                                             &env->fp_status);
        switch (cmp) {
        case float_relation_less:
            ST1 = floatx80_chs(ST1);
            break;
        case float_relation_greater:
            /* Result is infinity of the same sign as ST1.  */
            break;
        default:
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
            break;
        }
    } else if (floatx80_is_infinity(ST0)) {
        if (floatx80_is_zero(ST1)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
        } else if (arg1_sign) {
            ST1 = floatx80_chs(ST0);
        } else {
            ST1 = ST0;
        }
    } else if (floatx80_is_zero(ST0)) {
        if (floatx80_is_zero(ST1)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
        } else {
            /* Result is infinity with opposite sign to ST1.  */
            float_raise(float_flag_divbyzero, &env->fp_status);
            ST1 = make_floatx80(arg1_sign ? 0x7fff : 0xffff,
                                0x8000000000000000ULL);
        }
    } else if (floatx80_is_zero(ST1)) {
        if (floatx80_lt(ST0, floatx80_one, &env->fp_status)) {
            ST1 = floatx80_chs(ST1);
        }
        /* Otherwise, ST1 is already the correct result.  */
    } else if (floatx80_eq(ST0, floatx80_one, &env->fp_status)) {
        if (arg1_sign) {
            ST1 = floatx80_chs(floatx80_zero);
        } else {
            ST1 = floatx80_zero;
        }
    } else {
        int32_t int_exp;
        floatx80 arg0_m1;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        if (arg0_exp == 0) {
            normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
        }
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        int_exp = arg0_exp - 0x3fff;
        if (arg0_sig > 0xb504f333f9de6484ULL) {
            ++int_exp;
        }
        arg0_m1 = floatx80_sub(floatx80_scalbn(ST0, -int_exp,
                                               &env->fp_status),
                               floatx80_one, &env->fp_status);
        if (floatx80_is_zero(arg0_m1)) {
            /* Exact power of 2; multiply by ST1.  */
            env->fp_status.float_rounding_mode = save_mode;
            ST1 = floatx80_mul(int32_to_floatx80(int_exp, &env->fp_status),
                               ST1, &env->fp_status);
        } else {
            bool asign = extractFloatx80Sign(arg0_m1);
            int32_t aexp;
            uint64_t asig0, asig1, asig2;
            helper_fyl2x_common(env, arg0_m1, &aexp, &asig0, &asig1);
            if (int_exp != 0) {
                bool isign = (int_exp < 0);
                int32_t iexp;
                uint64_t isig;
                int shift;
                int_exp = isign ? -int_exp : int_exp;
                shift = clz32(int_exp) + 32;
                isig = int_exp;
                isig <<= shift;
                iexp = 0x403e - shift;
                shift128RightJamming(asig0, asig1, iexp - aexp,
                                     &asig0, &asig1);
                if (asign == isign) {
                    add128(isig, 0, asig0, asig1, &asig0, &asig1);
                } else {
                    sub128(isig, 0, asig0, asig1, &asig0, &asig1);
                }
                aexp = iexp;
                asign = isign;
            }
            /*
             * Multiply by the second argument to compute the required
             * result.
             */
            if (arg1_exp == 0) {
                normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
            }
            mul128By64To192(asig0, asig1, arg1_sig, &asig0, &asig1, &asig2);
            aexp += arg1_exp - 0x3ffe;
            /* This result is inexact.  */
            asig1 |= 1;
            env->fp_status.float_rounding_mode = save_mode;
            ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                asign ^ arg1_sign, aexp,
                                                asig0, asig1, &env->fp_status);
        }

        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fsqrt(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    if (floatx80_is_neg(ST0)) {
        env->fpus &= ~0x4700;  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = floatx80_sqrt(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsincos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        fpush(env);
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}

void helper_frndint(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_round_to_int(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fscale(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    if (floatx80_invalid_encoding(ST1) || floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST1)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
        }
        ST0 = ST1;
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
    } else if (floatx80_is_infinity(ST1) &&
               !floatx80_invalid_encoding(ST0) &&
               !floatx80_is_any_nan(ST0)) {
        if (floatx80_is_neg(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_zero) :
                       floatx80_zero);
            }
        } else {
            if (floatx80_is_zero(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_infinity) :
                       floatx80_infinity);
            }
        }
    } else {
        int n;
        FloatX80RoundPrec save = env->fp_status.floatx80_rounding_precision;
        uint8_t save_flags = get_float_exception_flags(&env->fp_status);
        set_float_exception_flags(0, &env->fp_status);
        n = floatx80_to_int32_round_to_zero(ST1, &env->fp_status);
        set_float_exception_flags(save_flags, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;
        ST0 = floatx80_scalbn(ST0, n, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fsin(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**53 only */
    }
}

void helper_fcos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}

void helper_fxam_ST0(CPUX86State *env)
{
    CPU_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp)) {
        env->fpus |= 0x200; /* C1 <-- 1 */
    }

    if (env->fptags[env->fpstt]) {
        env->fpus |= 0x4100; /* Empty */
        return;
    }

    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0x8000000000000000ULL) {
            env->fpus |= 0x500; /* Infinity */
        } else if (MANTD(temp) & 0x8000000000000000ULL) {
            env->fpus |= 0x100; /* NaN */
        }
    } else if (expdif == 0) {
        if (MANTD(temp) == 0) {
            env->fpus |=  0x4000; /* Zero */
        } else {
            env->fpus |= 0x4400; /* Denormal */
        }
    } else if (MANTD(temp) & 0x8000000000000000ULL) {
        env->fpus |= 0x400;
    }
}

static void do_fstenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int fpus, fptag, exp, i;
    uint64_t mant;
    CPU_LDoubleU tmp;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 7; i >= 0; i--) {
        fptag <<= 2;
        if (env->fptags[i]) {
            fptag |= 3;
        } else {
            tmp.d = env->fpregs[i].d;
            exp = EXPD(tmp);
            mant = MANTD(tmp);
            if (exp == 0 && mant == 0) {
                /* zero */
                fptag |= 1;
            } else if (exp == 0 || exp == MAXEXPD
                       || (mant & (1LL << 63)) == 0) {
                /* NaNs, infinity, denormal */
                fptag |= 2;
            }
        }
    }
    if (data32) {
        /* 32 bit */
        cpu_stl_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stl_data_ra(env, ptr + 4, fpus, retaddr);
        cpu_stl_data_ra(env, ptr + 8, fptag, retaddr);
        cpu_stl_data_ra(env, ptr + 12, env->fpip, retaddr); /* fpip */
        cpu_stl_data_ra(env, ptr + 16, env->fpcs, retaddr); /* fpcs */
        cpu_stl_data_ra(env, ptr + 20, env->fpdp, retaddr); /* fpoo */
        cpu_stl_data_ra(env, ptr + 24, env->fpds, retaddr); /* fpos */
    } else {
        /* 16 bit */
        cpu_stw_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stw_data_ra(env, ptr + 2, fpus, retaddr);
        cpu_stw_data_ra(env, ptr + 4, fptag, retaddr);
        cpu_stw_data_ra(env, ptr + 6, env->fpip, retaddr);
        cpu_stw_data_ra(env, ptr + 8, env->fpcs, retaddr);
        cpu_stw_data_ra(env, ptr + 10, env->fpdp, retaddr);
        cpu_stw_data_ra(env, ptr + 12, env->fpds, retaddr);
    }
}

void helper_fstenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fstenv(env, ptr, data32, GETPC());
}

static void cpu_set_fpus(CPUX86State *env, uint16_t fpus)
{
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800 & ~FPUS_B;
    env->fpus |= env->fpus & FPUS_SE ? FPUS_B : 0;
#if !defined(CONFIG_USER_ONLY)
    if (!(env->fpus & FPUS_SE)) {
        /*
         * Here the processor deasserts FERR#; in response, the chipset deasserts
         * IGNNE#.
         */
        cpu_clear_ignne();
    }
#endif
}

static void do_fldenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int i, fpus, fptag;

    if (data32) {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 4, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    } else {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 2, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 4, retaddr);
    }
    cpu_set_fpus(env, fpus);
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
}

void helper_fldenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fldenv(env, ptr, data32, GETPC());
}

static void do_fsave(CPUX86State *env, target_ulong ptr, int data32,
                     uintptr_t retaddr)
{
    floatx80 tmp;
    int i;

    do_fstenv(env, ptr, data32, retaddr);

    ptr += (target_ulong)14 << data32;
    for (i = 0; i < 8; i++) {
        tmp = ST(i);
        do_fstt(env, tmp, ptr, retaddr);
        ptr += 10;
    }

    do_fninit(env);
}

void helper_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fsave(env, ptr, data32, GETPC());
}

static void do_frstor(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    floatx80 tmp;
    int i;

    do_fldenv(env, ptr, data32, retaddr);
    ptr += (target_ulong)14 << data32;

    for (i = 0; i < 8; i++) {
        tmp = do_fldt(env, ptr, retaddr);
        ST(i) = tmp;
        ptr += 10;
    }
}

void helper_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    do_frstor(env, ptr, data32, GETPC());
}

#define XO(X)  offsetof(X86XSaveArea, X)

static void do_xsave_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int fpus, fptag, i;
    target_ulong addr;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 0; i < 8; i++) {
        fptag |= (env->fptags[i] << i);
    }

    cpu_stw_data_ra(env, ptr + XO(legacy.fcw), env->fpuc, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.fsw), fpus, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.ftw), fptag ^ 0xff, ra);

    /* In 32-bit mode this is eip, sel, dp, sel.
       In 64-bit mode this is rip, rdp.
       But in either case we don't write actual data, just zeros.  */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpip), 0, ra); /* eip+sel; rip */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), 0, ra); /* edp+sel; rdp */

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = ST(i);
        do_fstt(env, tmp, addr, ra);
        addr += 16;
    }
}

static void do_xsave_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    update_mxcsr_from_sse_status(env);
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr), env->mxcsr, ra);
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr_mask), 0x0000ffff, ra);
}

static void do_xsave_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        cpu_stq_data_ra(env, addr, env->xmm_regs[i].ZMM_Q(0), ra);
        cpu_stq_data_ra(env, addr + 8, env->xmm_regs[i].ZMM_Q(1), ra);
        addr += 16;
    }
}

static void do_xsave_ymmh(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++, ptr += 16) {
        cpu_stq_data_ra(env, ptr, env->xmm_regs[i].ZMM_Q(2), ra);
        cpu_stq_data_ra(env, ptr + 8, env->xmm_regs[i].ZMM_Q(3), ra);
    }
}

static void do_xsave_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        cpu_stq_data_ra(env, addr, env->bnd_regs[i].lb, ra);
        cpu_stq_data_ra(env, addr + 8, env->bnd_regs[i].ub, ra);
    }
}

static void do_xsave_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu),
                    env->bndcs_regs.cfgu, ra);
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts),
                    env->bndcs_regs.sts, ra);
}

static void do_xsave_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr, env->pkru, ra);
}

static void do_fxsave(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    do_xsave_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xsave_mxcsr(env, ptr, ra);
        /* Fast FXSAVE leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xsave_sse(env, ptr, ra);
        }
    }
}

void helper_fxsave(CPUX86State *env, target_ulong ptr)
{
    do_fxsave(env, ptr, GETPC());
}

static uint64_t get_xinuse(CPUX86State *env)
{
    uint64_t inuse = -1;

    /* For the most part, we don't track XINUSE.  We could calculate it
       here for all components, but it's probably less work to simply
       indicate in use.  That said, the state of BNDREGS is important
       enough to track in HFLAGS, so we might as well use that here.  */
    if ((env->hflags & HF_MPX_IU_MASK) == 0) {
       inuse &= ~XSTATE_BNDREGS_MASK;
    }
    return inuse;
}

static void do_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                     uint64_t inuse, uint64_t opt, uintptr_t ra)
{
    uint64_t old_bv, new_bv;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* Never save anything not enabled by XCR0.  */
    rfbm &= env->xcr0;
    opt &= rfbm;

    if (opt & XSTATE_FP_MASK) {
        do_xsave_fpu(env, ptr, ra);
    }
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that saving MXCSR is not suppressed by XSAVEOPT.  */
        do_xsave_mxcsr(env, ptr, ra);
    }
    if (opt & XSTATE_SSE_MASK) {
        do_xsave_sse(env, ptr, ra);
    }
    if (opt & XSTATE_YMM_MASK) {
        do_xsave_ymmh(env, ptr + XO(avx_state), ra);
    }
    if (opt & XSTATE_BNDREGS_MASK) {
        do_xsave_bndregs(env, ptr + XO(bndreg_state), ra);
    }
    if (opt & XSTATE_BNDCSR_MASK) {
        do_xsave_bndcsr(env, ptr + XO(bndcsr_state), ra);
    }
    if (opt & XSTATE_PKRU_MASK) {
        do_xsave_pkru(env, ptr + XO(pkru_state), ra);
    }

    /* Update the XSTATE_BV field.  */
    old_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);
    new_bv = (old_bv & ~rfbm) | (inuse & rfbm);
    cpu_stq_data_ra(env, ptr + XO(header.xstate_bv), new_bv, ra);
}

void helper_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xsave(env, ptr, rfbm, get_xinuse(env), -1, GETPC());
}

void helper_xsaveopt(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uint64_t inuse = get_xinuse(env);
    do_xsave(env, ptr, rfbm, inuse, inuse, GETPC());
}

static void do_xrstor_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, fpuc, fpus, fptag;
    target_ulong addr;

    fpuc = cpu_lduw_data_ra(env, ptr + XO(legacy.fcw), ra);
    fpus = cpu_lduw_data_ra(env, ptr + XO(legacy.fsw), ra);
    fptag = cpu_lduw_data_ra(env, ptr + XO(legacy.ftw), ra);
    cpu_set_fpuc(env, fpuc);
    cpu_set_fpus(env, fpus);
    fptag ^= 0xff;
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag >> i) & 1);
    }

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = do_fldt(env, addr, ra);
        ST(i) = tmp;
        addr += 16;
    }
}

static void do_xrstor_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_set_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra));
}

static void do_xrstor_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = cpu_ldq_data_ra(env, addr, ra);
        env->xmm_regs[i].ZMM_Q(1) = cpu_ldq_data_ra(env, addr + 8, ra);
        addr += 16;
    }
}

static void do_clear_sse(CPUX86State *env)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = 0;
        env->xmm_regs[i].ZMM_Q(1) = 0;
    }
}

static void do_xrstor_ymmh(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++, ptr += 16) {
        env->xmm_regs[i].ZMM_Q(2) = cpu_ldq_data_ra(env, ptr, ra);
        env->xmm_regs[i].ZMM_Q(3) = cpu_ldq_data_ra(env, ptr + 8, ra);
    }
}

static void do_clear_ymmh(CPUX86State *env)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}

static void do_xrstor_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        env->bnd_regs[i].lb = cpu_ldq_data_ra(env, addr, ra);
        env->bnd_regs[i].ub = cpu_ldq_data_ra(env, addr + 8, ra);
    }
}

static void do_xrstor_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    /* FIXME: Extend highest implemented bit of linear address.  */
    env->bndcs_regs.cfgu
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu), ra);
    env->bndcs_regs.sts
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts), ra);
}

static void do_xrstor_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    env->pkru = cpu_ldq_data_ra(env, ptr, ra);
}

static void do_fxrstor(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    do_xrstor_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xrstor_mxcsr(env, ptr, ra);
        /* Fast FXRSTOR leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xrstor_sse(env, ptr, ra);
        }
    }
}

void helper_fxrstor(CPUX86State *env, target_ulong ptr)
{
    do_fxrstor(env, ptr, GETPC());
}

static void do_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm, uintptr_t ra)
{
    uint64_t xstate_bv, xcomp_bv, reserve0;

    rfbm &= env->xcr0;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    xstate_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);

    if ((int64_t)xstate_bv < 0) {
        /* FIXME: Compact form.  */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* Standard form.  */

    /* The XSTATE_BV field must not set bits not present in XCR0.  */
    if (xstate_bv & ~env->xcr0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* The XCOMP_BV field must be zero.  Note that, as of the April 2016
       revision, the description of the XSAVE Header (Vol 1, Sec 13.4.2)
       describes only XCOMP_BV, but the description of the standard form
       of XRSTOR (Vol 1, Sec 13.8.1) checks bytes 23:8 for zero, which
       includes the next 64-bit field.  */
    xcomp_bv = cpu_ldq_data_ra(env, ptr + XO(header.xcomp_bv), ra);
    reserve0 = cpu_ldq_data_ra(env, ptr + XO(header.reserve0), ra);
    if (xcomp_bv || reserve0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    if (rfbm & XSTATE_FP_MASK) {
        if (xstate_bv & XSTATE_FP_MASK) {
            do_xrstor_fpu(env, ptr, ra);
        } else {
            do_fninit(env);
            memset(env->fpregs, 0, sizeof(env->fpregs));
        }
    }
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that the standard form of XRSTOR loads MXCSR from memory
           whether or not the XSTATE_BV bit is set.  */
        do_xrstor_mxcsr(env, ptr, ra);
        if (xstate_bv & XSTATE_SSE_MASK) {
            do_xrstor_sse(env, ptr, ra);
        } else {
            do_clear_sse(env);
        }
    }
    if (rfbm & XSTATE_YMM_MASK) {
        if (xstate_bv & XSTATE_YMM_MASK) {
            do_xrstor_ymmh(env, ptr + XO(avx_state), ra);
        } else {
            do_clear_ymmh(env);
        }
    }
    if (rfbm & XSTATE_BNDREGS_MASK) {
        if (xstate_bv & XSTATE_BNDREGS_MASK) {
            do_xrstor_bndregs(env, ptr + XO(bndreg_state), ra);
            env->hflags |= HF_MPX_IU_MASK;
        } else {
            memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
            env->hflags &= ~HF_MPX_IU_MASK;
        }
    }
    if (rfbm & XSTATE_BNDCSR_MASK) {
        if (xstate_bv & XSTATE_BNDCSR_MASK) {
            do_xrstor_bndcsr(env, ptr + XO(bndcsr_state), ra);
        } else {
            memset(&env->bndcs_regs, 0, sizeof(env->bndcs_regs));
        }
        cpu_sync_bndcs_hflags(env);
    }
    if (rfbm & XSTATE_PKRU_MASK) {
        uint64_t old_pkru = env->pkru;
        if (xstate_bv & XSTATE_PKRU_MASK) {
            do_xrstor_pkru(env, ptr + XO(pkru_state), ra);
        } else {
            env->pkru = 0;
        }
        if (env->pkru != old_pkru) {
            CPUState *cs = env_cpu(env);
            tlb_flush(cs);
        }
    }
}

#undef XO

void helper_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xrstor(env, ptr, rfbm, GETPC());
}

#if defined(CONFIG_USER_ONLY)
void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fsave(env, ptr, data32, 0);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    do_frstor(env, ptr, data32, 0);
}

void cpu_x86_fxsave(CPUX86State *env, target_ulong ptr)
{
    do_fxsave(env, ptr, 0);
}

void cpu_x86_fxrstor(CPUX86State *env, target_ulong ptr)
{
    do_fxrstor(env, ptr, 0);
}

void cpu_x86_xsave(CPUX86State *env, target_ulong ptr)
{
    do_xsave(env, ptr, -1, get_xinuse(env), -1, 0);
}

void cpu_x86_xrstor(CPUX86State *env, target_ulong ptr)
{
    do_xrstor(env, ptr, -1, 0);
}
#endif

uint64_t helper_xgetbv(CPUX86State *env, uint32_t ecx)
{
    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    switch (ecx) {
    case 0:
        return env->xcr0;
    case 1:
        if (env->features[FEAT_XSAVE] & CPUID_XSAVE_XGETBV1) {
            return env->xcr0 & get_xinuse(env);
        }
        break;
    }
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

void helper_xsetbv(CPUX86State *env, uint32_t ecx, uint64_t mask)
{
    uint32_t dummy, ena_lo, ena_hi;
    uint64_t ena;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    /* Only XCR0 is defined at present; the FPU may not be disabled.  */
    if (ecx != 0 || (mask & XSTATE_FP_MASK) == 0) {
        goto do_gpf;
    }

    /* Disallow enabling unimplemented features.  */
    cpu_x86_cpuid(env, 0x0d, 0, &ena_lo, &dummy, &dummy, &ena_hi);
    ena = ((uint64_t)ena_hi << 32) | ena_lo;
    if (mask & ~ena) {
        goto do_gpf;
    }

    /* Disallow enabling only half of MPX.  */
    if ((mask ^ (mask * (XSTATE_BNDCSR_MASK / XSTATE_BNDREGS_MASK)))
        & XSTATE_BNDCSR_MASK) {
        goto do_gpf;
    }

    env->xcr0 = mask;
    cpu_sync_bndcs_hflags(env);
    cpu_sync_avx_hflag(env);
    return;

 do_gpf:
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

/* MMX/SSE */
/* XXX: optimize by storing fptt and fptags in the static cpu state */

#define SSE_DAZ             0x0040
#define SSE_RC_SHIFT        13
#define SSE_RC_MASK         (3 << SSE_RC_SHIFT)
#define SSE_FZ              0x8000

void update_mxcsr_status(CPUX86State *env)
{
    uint32_t mxcsr = env->mxcsr;
    int rnd_type;

    /* set rounding mode */
    rnd_type = (mxcsr & SSE_RC_MASK) >> SSE_RC_SHIFT;
    set_x86_rounding_mode(rnd_type, &env->sse_status);

    /* Set exception flags.  */
    set_float_exception_flags((mxcsr & FPUS_IE ? float_flag_invalid : 0) |
                              (mxcsr & FPUS_ZE ? float_flag_divbyzero : 0) |
                              (mxcsr & FPUS_OE ? float_flag_overflow : 0) |
                              (mxcsr & FPUS_UE ? float_flag_underflow : 0) |
                              (mxcsr & FPUS_PE ? float_flag_inexact : 0),
                              &env->sse_status);

    /* set denormals are zero */
    set_flush_inputs_to_zero((mxcsr & SSE_DAZ) ? 1 : 0, &env->sse_status);

    /* set flush to zero */
    set_flush_to_zero((mxcsr & SSE_FZ) ? 1 : 0, &env->sse_status);
}

void update_mxcsr_from_sse_status(CPUX86State *env)
{
    uint8_t flags = get_float_exception_flags(&env->sse_status);
    /*
     * The MXCSR denormal flag has opposite semantics to
     * float_flag_input_denormal (the softfloat code sets that flag
     * only when flushing input denormals to zero, but SSE sets it
     * only when not flushing them to zero), so is not converted
     * here.
     */
    env->mxcsr |= ((flags & float_flag_invalid ? FPUS_IE : 0) |
                   (flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                   (flags & float_flag_overflow ? FPUS_OE : 0) |
                   (flags & float_flag_underflow ? FPUS_UE : 0) |
                   (flags & float_flag_inexact ? FPUS_PE : 0) |
                   (flags & float_flag_output_denormal ? FPUS_UE | FPUS_PE :
                    0));
}

void helper_update_mxcsr(CPUX86State *env)
{
    update_mxcsr_from_sse_status(env);
}

void helper_ldmxcsr(CPUX86State *env, uint32_t val)
{
    cpu_set_mxcsr(env, val);
}

void helper_enter_mmx(CPUX86State *env)
{
    env->fpstt = 0;
    *(uint32_t *)(env->fptags) = 0;
    *(uint32_t *)(env->fptags + 4) = 0;
}

void helper_emms(CPUX86State *env)
{
    /* set to empty state */
    *(uint32_t *)(env->fptags) = 0x01010101;
    *(uint32_t *)(env->fptags + 4) = 0x01010101;
}

#define SHIFT 0
#include "ops_sse.h"

#define SHIFT 1
#include "ops_sse.h"

#define SHIFT 2
#include "ops_sse.h"
