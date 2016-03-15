/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include <math.h>
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#define FPU_RC_MASK         0xc00
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
#define floatx80_l2e make_floatx80(0x3fff, 0xb8aa3b295c17f0bcLL)
#define floatx80_l2t make_floatx80(0x4000, 0xd49a784bcd1b8afeLL)

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

static inline floatx80 helper_fldt(CPUX86State *env, target_ulong ptr,
                                   uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.l.lower = cpu_ldq_data_ra(env, ptr, retaddr);
    temp.l.upper = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    return temp.d;
}

static inline void helper_fstt(CPUX86State *env, floatx80 f, target_ulong ptr,
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

static inline floatx80 helper_fdiv(CPUX86State *env, floatx80 a, floatx80 b)
{
    if (floatx80_is_zero(b)) {
        fpu_set_exception(env, FPUS_ZE);
    }
    return floatx80_div(a, b, &env->fp_status);
}

static void fpu_raise_exception(CPUX86State *env, uintptr_t retaddr)
{
    if (env->cr[0] & CR0_NE_MASK) {
        raise_exception_ra(env, EXCP10_COPR, retaddr);
    }
#if !defined(CONFIG_USER_ONLY)
    else {
        cpu_set_ferr(env);
    }
#endif
}

void helper_flds_FT0(CPUX86State *env, uint32_t val)
{
    union {
        float32 f;
        uint32_t i;
    } u;

    u.i = val;
    FT0 = float32_to_floatx80(u.f, &env->fp_status);
}

void helper_fldl_FT0(CPUX86State *env, uint64_t val)
{
    union {
        float64 f;
        uint64_t i;
    } u;

    u.i = val;
    FT0 = float64_to_floatx80(u.f, &env->fp_status);
}

void helper_fildl_FT0(CPUX86State *env, int32_t val)
{
    FT0 = int32_to_floatx80(val, &env->fp_status);
}

void helper_flds_ST0(CPUX86State *env, uint32_t val)
{
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
}

void helper_fldl_ST0(CPUX86State *env, uint64_t val)
{
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
}

void helper_fildl_ST0(CPUX86State *env, int32_t val)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int32_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildll_ST0(CPUX86State *env, int64_t val)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int64_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

uint32_t helper_fsts_ST0(CPUX86State *env)
{
    union {
        float32 f;
        uint32_t i;
    } u;

    u.f = floatx80_to_float32(ST0, &env->fp_status);
    return u.i;
}

uint64_t helper_fstl_ST0(CPUX86State *env)
{
    union {
        float64 f;
        uint64_t i;
    } u;

    u.f = floatx80_to_float64(ST0, &env->fp_status);
    return u.i;
}

int32_t helper_fist_ST0(CPUX86State *env)
{
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        val = -32768;
    }
    return val;
}

int32_t helper_fistl_ST0(CPUX86State *env)
{
    int32_t val;
    signed char old_exp_flags;

    old_exp_flags = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    set_float_exception_flags(get_float_exception_flags(&env->fp_status)
                                | old_exp_flags, &env->fp_status);
    return val;
}

int64_t helper_fistll_ST0(CPUX86State *env)
{
    int64_t val;
    signed char old_exp_flags;

    old_exp_flags = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);

    val = floatx80_to_int64(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    set_float_exception_flags(get_float_exception_flags(&env->fp_status)
                                | old_exp_flags, &env->fp_status);
    return val;
}

int32_t helper_fistt_ST0(CPUX86State *env)
{
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        val = -32768;
    }
    return val;
}

int32_t helper_fisttl_ST0(CPUX86State *env)
{
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    return val;
}

int64_t helper_fisttll_ST0(CPUX86State *env)
{
    int64_t val;

    val = floatx80_to_int64_round_to_zero(ST0, &env->fp_status);
    return val;
}

void helper_fldt_ST0(CPUX86State *env, target_ulong ptr)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = helper_fldt(env, ptr, GETPC());
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fstt_ST0(CPUX86State *env, target_ulong ptr)
{
    helper_fstt(env, ST0, ptr, GETPC());
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
    int ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
}

void helper_fucom_ST0_FT0(CPUX86State *env)
{
    int ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
}

static const int fcomi_ccval[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_fcomi_ST0_FT0(CPUX86State *env)
{
    int eflags;
    int ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
}

void helper_fucomi_ST0_FT0(CPUX86State *env)
{
    int eflags;
    int ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
}

void helper_fadd_ST0_FT0(CPUX86State *env)
{
    ST0 = floatx80_add(ST0, FT0, &env->fp_status);
}

void helper_fmul_ST0_FT0(CPUX86State *env)
{
    ST0 = floatx80_mul(ST0, FT0, &env->fp_status);
}

void helper_fsub_ST0_FT0(CPUX86State *env)
{
    ST0 = floatx80_sub(ST0, FT0, &env->fp_status);
}

void helper_fsubr_ST0_FT0(CPUX86State *env)
{
    ST0 = floatx80_sub(FT0, ST0, &env->fp_status);
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
    ST(st_index) = floatx80_add(ST(st_index), ST0, &env->fp_status);
}

void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = floatx80_mul(ST(st_index), ST0, &env->fp_status);
}

void helper_fsub_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = floatx80_sub(ST(st_index), ST0, &env->fp_status);
}

void helper_fsubr_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = floatx80_sub(ST0, ST(st_index), &env->fp_status);
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
    ST0 = floatx80_l2t;
}

void helper_fldl2e_ST0(CPUX86State *env)
{
    ST0 = floatx80_l2e;
}

void helper_fldpi_ST0(CPUX86State *env)
{
    ST0 = floatx80_pi;
}

void helper_fldlg2_ST0(CPUX86State *env)
{
    ST0 = floatx80_lg2;
}

void helper_fldln2_ST0(CPUX86State *env)
{
    ST0 = floatx80_ln2;
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

void update_fp_status(CPUX86State *env)
{
    int rnd_type;

    /* set rounding mode */
    switch (env->fpuc & FPU_RC_MASK) {
    default:
    case FPU_RC_NEAR:
        rnd_type = float_round_nearest_even;
        break;
    case FPU_RC_DOWN:
        rnd_type = float_round_down;
        break;
    case FPU_RC_UP:
        rnd_type = float_round_up;
        break;
    case FPU_RC_CHOP:
        rnd_type = float_round_to_zero;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
    switch ((env->fpuc >> 8) & 3) {
    case 0:
        rnd_type = 32;
        break;
    case 2:
        rnd_type = 64;
        break;
    case 3:
    default:
        rnd_type = 80;
        break;
    }
    set_floatx80_rounding_precision(rnd_type, &env->fp_status);
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

void helper_fninit(CPUX86State *env)
{
    env->fpus = 0;
    env->fpstt = 0;
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
    int v;
    target_ulong mem_ref, mem_end;
    int64_t val;

    val = floatx80_to_int64(ST0, &env->fp_status);
    mem_ref = ptr;
    mem_end = mem_ref + 9;
    if (val < 0) {
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
}

void helper_f2xm1(CPUX86State *env)
{
    double val = floatx80_to_double(env, ST0);

    val = pow(2.0, val) - 1.0;
    ST0 = double_to_floatx80(env, val);
}

void helper_fyl2x(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if (fptemp > 0.0) {
        fptemp = log(fptemp) / log(2.0); /* log2(ST) */
        fptemp *= floatx80_to_double(env, ST1);
        ST1 = double_to_floatx80(env, fptemp);
        fpop(env);
    } else {
        env->fpus &= ~0x4700;
        env->fpus |= 0x400;
    }
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

void helper_fpatan(CPUX86State *env)
{
    double fptemp, fpsrcop;

    fpsrcop = floatx80_to_double(env, ST1);
    fptemp = floatx80_to_double(env, ST0);
    ST1 = double_to_floatx80(env, atan2(fpsrcop, fptemp));
    fpop(env);
}

void helper_fxtract(CPUX86State *env)
{
    CPU_LDoubleU temp;

    temp.d = ST0;

    if (floatx80_is_zero(ST0)) {
        /* Easy way to generate -inf and raising division by 0 exception */
        ST0 = floatx80_div(floatx80_chs(floatx80_one), floatx80_zero,
                           &env->fp_status);
        fpush(env);
        ST0 = temp.d;
    } else {
        int expdif;

        expdif = EXPD(temp) - EXPBIAS;
        /* DP exponent bias */
        ST0 = int32_to_floatx80(expdif, &env->fp_status);
        fpush(env);
        BIASEXPONENT(temp);
        ST0 = temp.d;
    }
}

void helper_fprem1(CPUX86State *env)
{
    double st0, st1, dblq, fpsrcop, fptemp;
    CPU_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    signed long long int q;

    st0 = floatx80_to_double(env, ST0);
    st1 = floatx80_to_double(env, ST1);

    if (isinf(st0) || isnan(st0) || isnan(st1) || (st1 == 0.0)) {
        ST0 = double_to_floatx80(env, 0.0 / 0.0); /* NaN */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        return;
    }

    fpsrcop = st0;
    fptemp = st1;
    fpsrcop1.d = ST0;
    fptemp1.d = ST1;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);

    if (expdif < 0) {
        /* optimisation? taken from the AMD docs */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* ST0 is unchanged */
        return;
    }

    if (expdif < 53) {
        dblq = fpsrcop / fptemp;
        /* round dblq towards nearest integer */
        dblq = rint(dblq);
        st0 = fpsrcop - fptemp * dblq;

        /* convert dblq to q by truncating towards zero */
        if (dblq < 0.0) {
            q = (signed long long int)(-dblq);
        } else {
            q = (signed long long int)dblq;
        }

        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* (C0,C3,C1) <-- (q2,q1,q0) */
        env->fpus |= (q & 0x4) << (8 - 2);  /* (C0) <-- q2 */
        env->fpus |= (q & 0x2) << (14 - 1); /* (C3) <-- q1 */
        env->fpus |= (q & 0x1) << (9 - 0);  /* (C1) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif - 50);
        fpsrcop = (st0 / st1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0) ?
                  -(floor(fabs(fpsrcop))) : floor(fpsrcop);
        st0 -= (st1 * fpsrcop * fptemp);
    }
    ST0 = double_to_floatx80(env, st0);
}

void helper_fprem(CPUX86State *env)
{
    double st0, st1, dblq, fpsrcop, fptemp;
    CPU_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    signed long long int q;

    st0 = floatx80_to_double(env, ST0);
    st1 = floatx80_to_double(env, ST1);

    if (isinf(st0) || isnan(st0) || isnan(st1) || (st1 == 0.0)) {
        ST0 = double_to_floatx80(env, 0.0 / 0.0); /* NaN */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        return;
    }

    fpsrcop = st0;
    fptemp = st1;
    fpsrcop1.d = ST0;
    fptemp1.d = ST1;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);

    if (expdif < 0) {
        /* optimisation? taken from the AMD docs */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* ST0 is unchanged */
        return;
    }

    if (expdif < 53) {
        dblq = fpsrcop / fptemp; /* ST0 / ST1 */
        /* round dblq towards zero */
        dblq = (dblq < 0.0) ? ceil(dblq) : floor(dblq);
        st0 = fpsrcop - fptemp * dblq; /* fpsrcop is ST0 */

        /* convert dblq to q by truncating towards zero */
        if (dblq < 0.0) {
            q = (signed long long int)(-dblq);
        } else {
            q = (signed long long int)dblq;
        }

        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* (C0,C3,C1) <-- (q2,q1,q0) */
        env->fpus |= (q & 0x4) << (8 - 2);  /* (C0) <-- q2 */
        env->fpus |= (q & 0x2) << (14 - 1); /* (C3) <-- q1 */
        env->fpus |= (q & 0x1) << (9 - 0);  /* (C1) <-- q0 */
    } else {
        int N = 32 + (expdif % 32); /* as per AMD docs */

        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, (double)(expdif - N));
        fpsrcop = (st0 / st1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0) ?
                  -(floor(fabs(fpsrcop))) : floor(fpsrcop);
        st0 -= (st1 * fpsrcop * fptemp);
    }
    ST0 = double_to_floatx80(env, st0);
}

void helper_fyl2xp1(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp + 1.0) > 0.0) {
        fptemp = log(fptemp + 1.0) / log(2.0); /* log2(ST + 1.0) */
        fptemp *= floatx80_to_double(env, ST1);
        ST1 = double_to_floatx80(env, fptemp);
        fpop(env);
    } else {
        env->fpus &= ~0x4700;
        env->fpus |= 0x400;
    }
}

void helper_fsqrt(CPUX86State *env)
{
    if (floatx80_is_neg(ST0)) {
        env->fpus &= ~0x4700;  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = floatx80_sqrt(ST0, &env->fp_status);
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
    ST0 = floatx80_round_to_int(ST0, &env->fp_status);
}

void helper_fscale(CPUX86State *env)
{
    if (floatx80_is_any_nan(ST1)) {
        ST0 = ST1;
    } else {
        int n = floatx80_to_int32_round_to_zero(ST1, &env->fp_status);
        ST0 = floatx80_scalbn(ST0, n, &env->fp_status);
    }
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

    /* XXX: test fptags too */
    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0x8000000000000000ULL) {
            env->fpus |= 0x500; /* Infinity */
        } else {
            env->fpus |= 0x100; /* NaN */
        }
    } else if (expdif == 0) {
        if (MANTD(temp) == 0) {
            env->fpus |=  0x4000; /* Zero */
        } else {
            env->fpus |= 0x4400; /* Denormal */
        }
    } else {
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
        cpu_stl_data_ra(env, ptr + 12, 0, retaddr); /* fpip */
        cpu_stl_data_ra(env, ptr + 16, 0, retaddr); /* fpcs */
        cpu_stl_data_ra(env, ptr + 20, 0, retaddr); /* fpoo */
        cpu_stl_data_ra(env, ptr + 24, 0, retaddr); /* fpos */
    } else {
        /* 16 bit */
        cpu_stw_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stw_data_ra(env, ptr + 2, fpus, retaddr);
        cpu_stw_data_ra(env, ptr + 4, fptag, retaddr);
        cpu_stw_data_ra(env, ptr + 6, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 8, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 10, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 12, 0, retaddr);
    }
}

void helper_fstenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fstenv(env, ptr, data32, GETPC());
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
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
}

void helper_fldenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fldenv(env, ptr, data32, GETPC());
}

void helper_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    floatx80 tmp;
    int i;

    do_fstenv(env, ptr, data32, GETPC());

    ptr += (14 << data32);
    for (i = 0; i < 8; i++) {
        tmp = ST(i);
        helper_fstt(env, tmp, ptr, GETPC());
        ptr += 10;
    }

    /* fninit */
    env->fpus = 0;
    env->fpstt = 0;
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

void helper_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    floatx80 tmp;
    int i;

    do_fldenv(env, ptr, data32, GETPC());
    ptr += (14 << data32);

    for (i = 0; i < 8; i++) {
        tmp = helper_fldt(env, ptr, GETPC());
        ST(i) = tmp;
        ptr += 10;
    }
}

#if defined(CONFIG_USER_ONLY)
void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_fsave(env, ptr, data32);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_frstor(env, ptr, data32);
}
#endif

static void do_xsave_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int fpus, fptag, i;
    target_ulong addr;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 0; i < 8; i++) {
        fptag |= (env->fptags[i] << i);
    }
    cpu_stw_data_ra(env, ptr, env->fpuc, ra);
    cpu_stw_data_ra(env, ptr + 2, fpus, ra);
    cpu_stw_data_ra(env, ptr + 4, fptag ^ 0xff, ra);

    /* In 32-bit mode this is eip, sel, dp, sel.
       In 64-bit mode this is rip, rdp.
       But in either case we don't write actual data, just zeros.  */
    cpu_stq_data_ra(env, ptr + 0x08, 0, ra); /* eip+sel; rip */
    cpu_stq_data_ra(env, ptr + 0x10, 0, ra); /* edp+sel; rdp */

    addr = ptr + 0x20;
    for (i = 0; i < 8; i++) {
        floatx80 tmp = ST(i);
        helper_fstt(env, tmp, addr, ra);
        addr += 16;
    }
}

static void do_xsave_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stl_data_ra(env, ptr + 0x18, env->mxcsr, ra); /* mxcsr */
    cpu_stl_data_ra(env, ptr + 0x1c, 0x0000ffff, ra); /* mxcsr_mask */
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

    addr = ptr + 0xa0;
    for (i = 0; i < nb_xmm_regs; i++) {
        cpu_stq_data_ra(env, addr, env->xmm_regs[i].ZMM_Q(0), ra);
        cpu_stq_data_ra(env, addr + 8, env->xmm_regs[i].ZMM_Q(1), ra);
        addr += 16;
    }
}

static void do_xsave_bndregs(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        cpu_stq_data_ra(env, addr, env->bnd_regs[i].lb, ra);
        cpu_stq_data_ra(env, addr + 8, env->bnd_regs[i].ub, ra);
    }
}

static void do_xsave_bndcsr(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    cpu_stq_data_ra(env, addr, env->bndcs_regs.cfgu, ra);
    cpu_stq_data_ra(env, addr + 8, env->bndcs_regs.sts, ra);
}

static void do_xsave_pkru(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    cpu_stq_data_ra(env, addr, env->pkru, ra);
}

void helper_fxsave(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();

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
    if (opt & XSTATE_BNDREGS_MASK) {
        target_ulong off = x86_ext_save_areas[XSTATE_BNDREGS_BIT].offset;
        do_xsave_bndregs(env, ptr + off, ra);
    }
    if (opt & XSTATE_BNDCSR_MASK) {
        target_ulong off = x86_ext_save_areas[XSTATE_BNDCSR_BIT].offset;
        do_xsave_bndcsr(env, ptr + off, ra);
    }
    if (opt & XSTATE_PKRU_MASK) {
        target_ulong off = x86_ext_save_areas[XSTATE_PKRU_BIT].offset;
        do_xsave_pkru(env, ptr + off, ra);
    }

    /* Update the XSTATE_BV field.  */
    old_bv = cpu_ldq_data_ra(env, ptr + 512, ra);
    new_bv = (old_bv & ~rfbm) | (inuse & rfbm);
    cpu_stq_data_ra(env, ptr + 512, new_bv, ra);
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
    int i, fpus, fptag;
    target_ulong addr;

    cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, ra));
    fpus = cpu_lduw_data_ra(env, ptr + 2, ra);
    fptag = cpu_lduw_data_ra(env, ptr + 4, ra);
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    fptag ^= 0xff;
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag >> i) & 1);
    }

    addr = ptr + 0x20;
    for (i = 0; i < 8; i++) {
        floatx80 tmp = helper_fldt(env, addr, ra);
        ST(i) = tmp;
        addr += 16;
    }
}

static void do_xrstor_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_set_mxcsr(env, cpu_ldl_data_ra(env, ptr + 0x18, ra));
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

    addr = ptr + 0xa0;
    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = cpu_ldq_data_ra(env, addr, ra);
        env->xmm_regs[i].ZMM_Q(1) = cpu_ldq_data_ra(env, addr + 8, ra);
        addr += 16;
    }
}

static void do_xrstor_bndregs(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        env->bnd_regs[i].lb = cpu_ldq_data_ra(env, addr, ra);
        env->bnd_regs[i].ub = cpu_ldq_data_ra(env, addr + 8, ra);
    }
}

static void do_xrstor_bndcsr(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    /* FIXME: Extend highest implemented bit of linear address.  */
    env->bndcs_regs.cfgu = cpu_ldq_data_ra(env, addr, ra);
    env->bndcs_regs.sts = cpu_ldq_data_ra(env, addr + 8, ra);
}

static void do_xrstor_pkru(CPUX86State *env, target_ulong addr, uintptr_t ra)
{
    env->pkru = cpu_ldq_data_ra(env, addr, ra);
}

void helper_fxrstor(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();

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

void helper_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uintptr_t ra = GETPC();
    uint64_t xstate_bv, xcomp_bv0, xcomp_bv1;

    rfbm &= env->xcr0;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    xstate_bv = cpu_ldq_data_ra(env, ptr + 512, ra);

    if ((int64_t)xstate_bv < 0) {
        /* FIXME: Compact form.  */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* Standard form.  */

    /* The XSTATE field must not set bits not present in XCR0.  */
    if (xstate_bv & ~env->xcr0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* The XCOMP field must be zero.  */
    xcomp_bv0 = cpu_ldq_data_ra(env, ptr + 520, ra);
    xcomp_bv1 = cpu_ldq_data_ra(env, ptr + 528, ra);
    if (xcomp_bv0 || xcomp_bv1) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    if (rfbm & XSTATE_FP_MASK) {
        if (xstate_bv & XSTATE_FP_MASK) {
            do_xrstor_fpu(env, ptr, ra);
        } else {
            helper_fninit(env);
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
            /* ??? When AVX is implemented, we may have to be more
               selective in the clearing.  */
            memset(env->xmm_regs, 0, sizeof(env->xmm_regs));
        }
    }
    if (rfbm & XSTATE_BNDREGS_MASK) {
        if (xstate_bv & XSTATE_BNDREGS_MASK) {
            target_ulong off = x86_ext_save_areas[XSTATE_BNDREGS_BIT].offset;
            do_xrstor_bndregs(env, ptr + off, ra);
            env->hflags |= HF_MPX_IU_MASK;
        } else {
            memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
            env->hflags &= ~HF_MPX_IU_MASK;
        }
    }
    if (rfbm & XSTATE_BNDCSR_MASK) {
        if (xstate_bv & XSTATE_BNDCSR_MASK) {
            target_ulong off = x86_ext_save_areas[XSTATE_BNDCSR_BIT].offset;
            do_xrstor_bndcsr(env, ptr + off, ra);
        } else {
            memset(&env->bndcs_regs, 0, sizeof(env->bndcs_regs));
        }
        cpu_sync_bndcs_hflags(env);
    }
    if (rfbm & XSTATE_PKRU_MASK) {
        uint64_t old_pkru = env->pkru;
        if (xstate_bv & XSTATE_PKRU_MASK) {
            target_ulong off = x86_ext_save_areas[XSTATE_PKRU_BIT].offset;
            do_xrstor_pkru(env, ptr + off, ra);
        } else {
            env->pkru = 0;
        }
        if (env->pkru != old_pkru) {
            CPUState *cs = CPU(x86_env_get_cpu(env));
            tlb_flush(cs, 1);
        }
    }
}

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
    return;

 do_gpf:
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, floatx80 f)
{
    CPU_LDoubleU temp;

    temp.d = f;
    *pmant = temp.l.lower;
    *pexp = temp.l.upper;
}

floatx80 cpu_set_fp80(uint64_t mant, uint16_t upper)
{
    CPU_LDoubleU temp;

    temp.l.upper = upper;
    temp.l.lower = mant;
    return temp.d;
}

/* MMX/SSE */
/* XXX: optimize by storing fptt and fptags in the static cpu state */

#define SSE_DAZ             0x0040
#define SSE_RC_MASK         0x6000
#define SSE_RC_NEAR         0x0000
#define SSE_RC_DOWN         0x2000
#define SSE_RC_UP           0x4000
#define SSE_RC_CHOP         0x6000
#define SSE_FZ              0x8000

void cpu_set_mxcsr(CPUX86State *env, uint32_t mxcsr)
{
    int rnd_type;

    env->mxcsr = mxcsr;

    /* set rounding mode */
    switch (mxcsr & SSE_RC_MASK) {
    default:
    case SSE_RC_NEAR:
        rnd_type = float_round_nearest_even;
        break;
    case SSE_RC_DOWN:
        rnd_type = float_round_down;
        break;
    case SSE_RC_UP:
        rnd_type = float_round_up;
        break;
    case SSE_RC_CHOP:
        rnd_type = float_round_to_zero;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->sse_status);

    /* set denormals are zero */
    set_flush_inputs_to_zero((mxcsr & SSE_DAZ) ? 1 : 0, &env->sse_status);

    /* set flush to zero */
    set_flush_to_zero((mxcsr & SSE_FZ) ? 1 : 0, &env->fp_status);
}

void cpu_set_fpuc(CPUX86State *env, uint16_t val)
{
    env->fpuc = val;
    update_fp_status(env);
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

/* XXX: suppress */
void helper_movq(CPUX86State *env, void *d, void *s)
{
    *(uint64_t *)d = *(uint64_t *)s;
}

#define SHIFT 0
#include "ops_sse.h"

#define SHIFT 1
#include "ops_sse.h"
