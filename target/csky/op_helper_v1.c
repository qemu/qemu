/*
 *  C-sky_v1 helper routines
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
#include "cpu.h"
#include "translate.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

void helper_exception(CPUCSKYState *env, uint32_t excp)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

void helper_tb_trace(CPUCSKYState *env, uint32_t tb_pc)
{
    int trace_index = env->trace_index % TB_TRACE_NUM;
    env->trace_info[trace_index].tb_pc = tb_pc;
    env->trace_index++;
    qemu_log_mask(CPU_TB_TRACE, "0x%.8x\n", tb_pc);
}

#ifdef CONFIG_USER_ONLY
extern long long total_jcount;
void helper_jcount(CPUCSKYState *env, uint32_t tb_pc, uint32_t icount)
{
    if ((tb_pc >= env->jcount_start) && (tb_pc < env->jcount_end)) {
        total_jcount += icount;
    }
}
#else
void helper_jcount(CPUCSKYState *env, uint32_t tb_pc, uint32_t icount)
{
}
#endif

uint32_t helper_brev(uint32_t a)
{
    uint32_t r = 0;
    char  i;
    for (i = 0; i < 32; i++) {
        r |= (a & 0x1) << (31 - i);
        a >>= 1;
    }
    return r;
}

uint32_t helper_ff1(uint32_t a)
{
    if (a == 0) {
        return 32;
    } else {
        return __builtin_clz(a);
    }
}

#if !defined(CONFIG_USER_ONLY)
void helper_rte(CPUCSKYState *env)
{
    if ((env->cp0.psr & 0x2) != (env->cp0.epsr & 0x2)) {
        helper_switch_regs(env);
    }
    env->cp0.psr = env->cp0.epsr & ~(0x7f << 16);
    env->psr_s = PSR_S(env->cp0.psr);
    env->psr_c = PSR_C(env->cp0.psr);
    env->psr_tm = PSR_TM(env->cp0.psr);

    if (unlikely(PSR_TP(env->cp0.epsr))) {
        env->cp0.psr |= EXCP_CSKY_TRACE << 16;
        env->cp0.epsr = (env->cp0.psr & ~0x8000e001)    /* clear TP in EPSR */
            | (env->psr_s << 31)
            | (env->psr_c)
            | (env->psr_tm << 14);
        env->psr_s = 1;
        env->psr_tm = 0;
        env->cp0.psr &= ~PSR_TP_MASK;
        env->cp0.psr &= ~PSR_EE_MASK;
        env->cp0.psr &= ~PSR_IE_MASK;

        env->pc = cpu_ldl_code(env, env->cp0.vbr + EXCP_CSKY_TRACE * 4);
        if (unlikely((env->pc & 0x1) != ((env->cp0.psr & 0x2) >> 1))) {
            helper_switch_regs(env);
            env->cp0.psr |= (env->pc & 0x1) << 1;
        }
        env->pc &= ~0x1;
    } else {
        env->pc = env->cp0.epc;
    }

}

void helper_rfi(CPUCSKYState *env)
{
    if ((env->cp0.psr & 0x2) != (env->cp0.fpsr & 0x2)) {
        helper_switch_regs(env);
    }
    env->cp0.psr = env->cp0.fpsr & ~(0x7f << 16);
    env->psr_s = PSR_S(env->cp0.psr);
    env->psr_c = PSR_C(env->cp0.psr);
    env->psr_tm = PSR_TM(env->cp0.psr);
    if (unlikely(PSR_TP(env->cp0.fpsr))) {
        env->cp0.psr |= EXCP_CSKY_TRACE << 16;
        env->cp0.epsr = (env->cp0.psr & ~0x8000e001)   /* clear TP in FPSR */
            | (env->psr_s << 31)
            | (env->psr_c)
            | (env->psr_tm << 14);
        env->psr_s = 1;
        env->psr_tm = 0;
        env->cp0.psr &= ~PSR_TP_MASK;
        env->cp0.psr &= ~PSR_EE_MASK;
        env->cp0.psr &= ~PSR_IE_MASK;

        env->pc = cpu_ldl_code(env, env->cp0.vbr + EXCP_CSKY_TRACE * 4);
        if (unlikely((env->pc & 0x1) != ((env->cp0.psr & 0x2) >> 1))) {
            helper_switch_regs(env);
            env->cp0.psr |= (env->pc & 0x1) << 1;
        }
        env->pc &= ~0x1;
    } else {
        env->pc = env->cp0.fpc;
    }
}


void helper_psrclr(CPUCSKYState *env, uint32_t imm)
{
    /* AF bit */
    if (!imm && (env->cp0.psr & 0x2)) {
        env->cp0.psr &= ~0x2;
        helper_switch_regs(env);
    }
    /* IE bit */
    if (imm & 0x1) {
        env->cp0.psr &= ~0x40;
    }
    /* FE bit */
    if (imm & 0x2) {
        env->cp0.psr &= ~0x10;
    }
    /* EE bit */
    if (imm & 0x4) {
        env->cp0.psr &= ~0x100;
    }
}


void helper_psrset(CPUCSKYState *env, uint32_t imm)
{
    /* AF bit */
    if (!imm && !(env->cp0.psr & 0x2)) {
        env->cp0.psr |= 0x2;
        helper_switch_regs(env);
    }
    /* IE bit */
    if (imm & 0x1) {
        env->cp0.psr |= 0x40;
    }
    /* FE bit */
    if (imm & 0x2) {
        env->cp0.psr |= 0x10;
    }
    /* EE bit */
    if (imm & 0x4) {
        env->cp0.psr |= 0x100;
    }
}

void helper_stop(CPUCSKYState *env)
{
    CPUState *cs;
    cs = CPU(csky_env_get_cpu(env));
    cs->halted = 1;
    helper_exception(env, EXCP_HLT);
}

void helper_wait(CPUCSKYState *env)
{
    CPUState *cs;
    cs = CPU(csky_env_get_cpu(env));
    cs->halted = 1;
    helper_exception(env, EXCP_HLT);
}

void helper_doze(CPUCSKYState *env)
{
    CPUState *cs;
    cs = CPU(csky_env_get_cpu(env));
    cs->halted = 1;
    helper_exception(env, EXCP_HLT);
}
#endif

void helper_cprc(CPUCSKYState *env)
{
    env->psr_c = PSR_C(env->cp1.fsr);
}

#define SETFC (env->cp1.fsr = env->cp1.fsr | 0x1)

#define CLEARFC  (env->cp1.fsr = env->cp1.fsr & (~0x1))

#define getfloat64(env, n) ((float64)(env->cp1.fr[n]) | \
                      ((float64)(env->cp1.fr[n + 1]) << 32))

static void cmp_ge_s(CPUCSKYState *env, float32 a, float32 b)
{
    switch (float32_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        SETFC;
        break;
    case -1:
        CLEARFC;
        break;
    case 1:
        SETFC;
        break;
    case 2:
    default:
        CLEARFC;
        break;
    }
}

static void cmp_ge_d(CPUCSKYState *env, float64 a, float64 b)
{
    switch (float64_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        SETFC;
        break;
    case -1:
        CLEARFC;
        break;
    case 1:
        SETFC;
        break;
    case 2:
    default:
        CLEARFC;
        break;
    }
}

static void fcmpzhsd(CPUCSKYState *env, int src0)
{
    float64 src1 = 0;
    cmp_ge_d(env, getfloat64(env, src0), src1);
}

static void fcmpzhss(CPUCSKYState *env, int src0)
{
    float32 src1 = 0;
    cmp_ge_s(env, env->cp1.fr[src0], src1);
}

static void fcmphss(CPUCSKYState *env, int src0, int src1)
{
    cmp_ge_s(env, env->cp1.fr[src0], env->cp1.fr[src1]);
}

static void fcmphsd(CPUCSKYState *env, int src0, int src1)
{
    cmp_ge_d(env, getfloat64(env, src0), getfloat64(env, src1));
}

static void cmp_l_s(CPUCSKYState *env, float32 a, float32 b)
{
    switch (float32_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        SETFC;
        break;
    case 1:
        CLEARFC;
        break;
    case 2:
    default:
        CLEARFC;
        break;
    }
}

static void cmp_l_d(CPUCSKYState *env, float64 a, float64 b)
{
    switch (float64_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        SETFC;
        break;
    case 1:
        CLEARFC;
        break;
    case 2:
    default:
        CLEARFC;
        break;
    }
}

static void fcmpzltd(CPUCSKYState *env, int src0)
{
    float64 src1 = 0;
    cmp_l_d(env, getfloat64(env, src0), src1);
}

static void fcmpzlts(CPUCSKYState *env, int src0)
{
    float32 src1 = 0;
    cmp_l_s(env, env->cp1.fr[src0], src1);
}

static void fcmplts(CPUCSKYState *env, int src0, int src1)
{
    cmp_l_s(env, env->cp1.fr[src0], env->cp1.fr[src1]);
}

static void fcmpltd(CPUCSKYState *env, int src0, int src1)
{
    cmp_l_d(env, getfloat64(env, src0), getfloat64(env, src1));
}

static void cmp_ne_s(CPUCSKYState *env, float32 a, float32 b)
{
    switch (float32_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        SETFC;
        break;
    case 1:
        SETFC;
        break;
    case 2:
    default:
        SETFC;
        break;
    }
}

static void cmp_ne_d(CPUCSKYState *env, float64 a, float64 b)
{
    switch (float64_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        SETFC;
        break;
    case 1:
        SETFC;
        break;
    case 2:
    default:
        SETFC;
        break;
    }
}

static void fcmpzned(CPUCSKYState *env, int src0)
{
    float64 src1 = 0;
    cmp_ne_d(env, getfloat64(env, src0), src1);
}

static void fcmpznes(CPUCSKYState *env, int src0)
{
    float32 src1 = 0;
    cmp_ne_s(env, env->cp1.fr[src0], src1);

}

static void fcmpnes(CPUCSKYState *env, int src0, int src1)
{
    cmp_ne_s(env, env->cp1.fr[src0], env->cp1.fr[src1]);
}

static void fcmpned(CPUCSKYState *env, int src0, int src1)
{
    cmp_ne_d(env, getfloat64(env, src0), getfloat64(env, src1));
}

static void cmp_isNAN_s(CPUCSKYState *env, float32 a, float32 b)
{
    switch (float32_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        CLEARFC;
        break;
    case 1:
        CLEARFC;
        break;
    case 2:
    default:
        SETFC;
        break;
    }
}

static void cmp_isNAN_d(CPUCSKYState *env, float64 a,  float64 b)
{
    switch (float64_compare_quiet(a, b, &env->cp1.fp_status)) {
    case 0:
        CLEARFC;
        break;
    case -1:
        CLEARFC;
        break;
    case 1:
        CLEARFC;
        break;
    case 2:
    default:
        SETFC;
        break;
    }
}

static void fcmpzuod(CPUCSKYState *env, int src0)
{
    float64 src1 = 0;
    cmp_isNAN_d(env, getfloat64(env, src0), src1);
}

static void fcmpzuos(CPUCSKYState *env, int src0)
{
    float32 src1 = 0;
    cmp_isNAN_s(env, env->cp1.fr[src0], src1);
}

static void fcmpuos(CPUCSKYState *env, int src0, int src1)
{
    cmp_isNAN_s(env, env->cp1.fr[src0], env->cp1.fr[src1]);
}

static void fcmpuod(CPUCSKYState *env, int src0, int src1)
{
    cmp_isNAN_d(env, getfloat64(env, src0), getfloat64(env, src1));
}


/* floating point conversion */
static float64 stod(CPUCSKYState *env, float32 x)
{
    float64 r = float32_to_float64(x, &env->cp1.fp_status);
    /* ARM requires that S<->D conversion of any kind of NaN generates
     * a quiet NaN by forcing the most significant frac bit to 1.
     */
    return float64_maybe_silence_nan(r, &env->cp1.fp_status);
}

static float32 dtos(CPUCSKYState *env, float64 x)
{
    float32 r =  float64_to_float32(x, &env->cp1.fp_status);
    /* ARM requires that S<->D conversion of any kind of NaN generates
     * a quiet NaN by forcing the most significant frac bit to 1.
     */
    return float32_maybe_silence_nan(r, &env->cp1.fp_status);
}

#define getfloat32lo(dst, x) (env->cp1.fr[dst] = (float32)(x & 0xffffffff))
#define getfloat32hi(dst, x) (env->cp1.fr[dst + 1] = (float32)(x >> 32))

static void fstod(CPUCSKYState *env, int dst, int src)
{
    float64 x;
    x = stod(env, env->cp1.fr[src]);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fdtos(CPUCSKYState *env, int dst, int src)
{
    float64 x;
    x = getfloat64(env, src);
    env->cp1.fr[dst] = dtos(env, x);
}

/* Helper routines to perform bitwise copies between float and int.  */
static inline float32 vfp_itos(uint32_t i)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.i = i;
    return v.s;
}

static inline uint32_t vfp_stoi(float32 s)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.s = s;
    return v.i;
}

static inline float64 vfp_itod(uint64_t i)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.i = i;
    return v.d;
}

static inline uint64_t vfp_dtoi(float64 d)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.d = d;
    return v.i;
}

/* Integer to float conversion.  */
static float32 uitos(CPUCSKYState *env, float32 x)
{
    return uint32_to_float32(vfp_stoi(x), &env->cp1.fp_status);
}

static float64 uitod(CPUCSKYState *env, float32 x)
{
    return uint32_to_float64(vfp_stoi(x), &env->cp1.fp_status);
}

static float32 sitos(CPUCSKYState *env, float32 x)
{
    return int32_to_float32(vfp_stoi(x), &env->cp1.fp_status);
}

static float64 sitod(CPUCSKYState *env, float32 x)
{
    return int32_to_float64(vfp_stoi(x), &env->cp1.fp_status);
}

static void fsitod(CPUCSKYState *env, int dst, int src)
{
    float64 x = sitod(env, env->cp1.fr[src]);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fsitos(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = sitos(env, env->cp1.fr[src]);
}

static void fuitod(CPUCSKYState *env, int dst, int src)
{
    float64 x = uitod(env, env->cp1.fr[src]);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fuitos(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = uitos(env, env->cp1.fr[src]);
}

static void fabsd(CPUCSKYState *env, int dst, int src)
{
    float64 x = getfloat64(env, src);
    x = float64_abs(x);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fabss(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = float32_abs(env->cp1.fr[src]);
}

static void fnegd(CPUCSKYState *env, int dst, int src)
{
    float64 x = getfloat64(env, src);
    x = float64_chs(x);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fnegs(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = float32_chs(env->cp1.fr[src]);
}

static void fsqrtd(CPUCSKYState *env, int dst, int src)
{
    float64 x = getfloat64(env, src);
    x = float64_sqrt(x, &env->cp1.fp_status);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fsqrts(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = float32_sqrt(env->cp1.fr[src], &env->cp1.fp_status);
}

static float32 recips(CPUCSKYState *env, float32 a)
{
    float_status *s = &env->cp1.fp_status;
    float32 one = int32_to_float32(1, s);
    return float32_div(one, a, s);
}

static float64 recipd(CPUCSKYState *env, float64 a)
{
    float_status *s = &env->cp1.fp_status;
    float64 one = int32_to_float64(1, s);
    return float64_div(one, a, s);
}

static void frecipd(CPUCSKYState *env, int dst, int src)
{
    float64 x = getfloat64(env, src);
    x = recipd(env, x);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void frecips(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = recips(env, env->cp1.fr[src]);
}

static void fabsm(CPUCSKYState *env, int dst, int src)
{
    fabss(env, dst, src);
    fabss(env, dst + 1, src + 1);
}

static void fnegm(CPUCSKYState *env, int dst, int src)
{
    fnegs(env, dst, src);
    fnegs(env, dst + 1, src + 1);
}


static void fmovd(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = env->cp1.fr[src];
    env->cp1.fr[dst + 1] = env->cp1.fr[src + 1];
}

static void fmovs(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = env->cp1.fr[src];
}

static float32 stosirn(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_int32(x, &env->cp1.fp_status));
}

static void fstosirn(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stosirn(env, env->cp1.fr[src]);
}

static float32 stosirz(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_int32_round_to_zero(x, &env->cp1.fp_status));
}

static void fstosirz(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stosirz(env, env->cp1.fr[src]);
}

static float32 stosirpi(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float32_to_int32_round_to_zero(x,
                                &env->cp1.fp_status) + 1);
    } else {
        return vfp_itos(float32_to_int32_round_to_zero(x,
                                    &env->cp1.fp_status));
    }
}

static void fstosirpi(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stosirpi(env, env->cp1.fr[src]);
}

static float32 stosirni(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float32_to_int32_round_to_zero(x,
                                    &env->cp1.fp_status));
    } else {
        return vfp_itos(float32_to_int32_round_to_zero(x,
                                &env->cp1.fp_status) - 1);
    }
}

static void fstosirni(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stosirni(env, env->cp1.fr[src]);
}

static float32 dtosirn(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_int32(x, &env->cp1.fp_status));
}

static void fdtosirn(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtosirn(env, getfloat64(env, src));
}

static float32 dtosirz(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_int32_round_to_zero(x, &env->cp1.fp_status));
}

static void fdtosirz(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtosirz(env, getfloat64(env, src));
}

static float32 dtosirpi(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float64_to_int32_round_to_zero(x,
                                &env->cp1.fp_status) + 1);
    } else {
        return vfp_itos(float64_to_int32_round_to_zero(x,
                                    &env->cp1.fp_status));
    }
}

static void fdtosirpi(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtosirpi(env, getfloat64(env, src));
}

static float32 dtosirni(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float64_to_int32_round_to_zero(x,
                                    &env->cp1.fp_status));
    } else {
        return vfp_itos(float64_to_int32_round_to_zero(x,
                               &env->cp1.fp_status) - 1);
    }
}

static void fdtosirni(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtosirni(env, getfloat64(env, src));
}

static float32 stouirn(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_uint32(x, &env->cp1.fp_status));
}

static void fstouirn(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stouirn(env, env->cp1.fr[src]);
}

static float32 stouirz(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_uint32_round_to_zero(x, &env->cp1.fp_status));
}

static void fstouirz(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stouirz(env, env->cp1.fr[src]);
}

static float32 stouirpi(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float32_to_uint32_round_to_zero(x,
                                 &env->cp1.fp_status) + 1);
    } else {
        return vfp_itos(float32_to_uint32_round_to_zero(x,
                                     &env->cp1.fp_status));
    }
}

static void fstouirpi(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stouirpi(env, env->cp1.fr[src]);
}

static float32 stouirni(CPUCSKYState *env, float32 x)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float32_to_uint32_round_to_zero(x,
                                     &env->cp1.fp_status));
    } else {
        return vfp_itos(float32_to_uint32_round_to_zero(x,
                                 &env->cp1.fp_status) - 1);
    }
}

static void fstouirni(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = stouirni(env, env->cp1.fr[src]);
}

static float32 dtouirn(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_uint32(x, &env->cp1.fp_status));

}

static void fdtouirn(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtouirn(env, getfloat64(env, src));
}

static float32 dtouirz(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_uint32_round_to_zero(x, &env->cp1.fp_status));

}

static void fdtouirz(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtouirz(env, getfloat64(env, src));
}

static float32 dtouirpi(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float64_to_uint32_round_to_zero(x,
                                 &env->cp1.fp_status) + 1);
    } else {
        return vfp_itos(float64_to_uint32_round_to_zero(x,
                                     &env->cp1.fp_status));
    }
}

static void fdtouirpi(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtouirpi(env, getfloat64(env, src));
}

static float32 dtouirni(CPUCSKYState *env, float64 x)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (x > 0) {
        return vfp_itos(float64_to_uint32_round_to_zero(x,
                                     &env->cp1.fp_status));
    } else {
        return vfp_itos(float64_to_uint32_round_to_zero(x,
                                 &env->cp1.fp_status) - 1);
    }
}

static void fdtouirni(CPUCSKYState *env, int dst, int src)
{
    env->cp1.fr[dst] = dtouirni(env, getfloat64(env, src));
}

#define VFP(name, p) (glue(name, p))

#define VFP_BINOP(name) \
static float32 VFP(name, s)(float32 a, float32 b, CPUCSKYState *env) \
{ \
    return float32_ ## name(a, b, &env->cp1.fp_status); \
} \
static float64 VFP(name, d)(float64 a, float64 b, CPUCSKYState *env) \
{ \
    return float64_ ## name(a, b, &env->cp1.fp_status); \
}
VFP_BINOP(add)
VFP_BINOP(sub)
VFP_BINOP(mul)
VFP_BINOP(div)
#undef VFP_BINOP

static void faddd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = addd(getfloat64(env, src0), getfloat64(env, src1), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fadds(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = adds(env->cp1.fr[src0], env->cp1.fr[src1], env);
}

static void fsubd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = subd(getfloat64(env, src0), getfloat64(env, src1), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fsubs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = subs(env->cp1.fr[src0], env->cp1.fr[src1], env);
}

static void fmuld(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = muld(getfloat64(env, src0), getfloat64(env, src1), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fmuls(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = muls(env->cp1.fr[src0], env->cp1.fr[src1], env);
}

static void fdivd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = divd(getfloat64(env, src0), getfloat64(env, src1), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fdivs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = divs(env->cp1.fr[src0], env->cp1.fr[src1], env);
}

static void fmacd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = addd(getfloat64(env, dst), muld(getfloat64(env, src0),
                                           getfloat64(env, src1), env), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fmacs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = adds(env->cp1.fr[dst], muls(env->cp1.fr[src0],
                                        env->cp1.fr[src1], env), env);
}

static void fmscd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = subd(muld(getfloat64(env, src0), getfloat64(env, src1), env),
                                            getfloat64(env, dst), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fmscs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = subs(muls(env->cp1.fr[src0],
                                 env->cp1.fr[src1], env),
                                 env->cp1.fr[dst], env);
}

static void fnmacd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = subd(getfloat64(env, dst), muld(getfloat64(env, src0),
                                getfloat64(env, src1), env), env);
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fnmacs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = subs(env->cp1.fr[dst], muls(env->cp1.fr[src0],
                                        env->cp1.fr[src1], env), env);
}

static void fnmscd(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = float64_chs(addd(muld(getfloat64(env, src0),
                                      getfloat64(env, src1), env),
                                      getfloat64(env, dst), env));
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fnmscs(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = float32_chs(adds(muls(env->cp1.fr[src0],
                                             env->cp1.fr[src1], env),
                                             env->cp1.fr[dst], env));
}

static void fnmuld(CPUCSKYState *env, int dst, int src0, int src1)
{
    float64 x = float64_chs(muld(getfloat64(env, src0),
                                 getfloat64(env, src1), env));
    getfloat32lo(dst, x);
    getfloat32hi(dst, x);
}

static void fnmuls(CPUCSKYState *env, int dst, int src0, int src1)
{
    env->cp1.fr[dst] = float32_chs(muls(env->cp1.fr[src0],
                                        env->cp1.fr[src1], env));
}

static void faddm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fadds(env, dst, src0, src1);
    fadds(env, dst + 1, src0 + 1, src1 + 1);
}

static void fsubm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fsubs(env, dst, src0, src1);
    fsubs(env, dst + 1, src0 + 1, src1 + 1);
}

static void fmulm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fmuls(env, dst, src0, src1);
    fmuls(env, dst + 1, src0 + 1, src1 + 1);
}

static void fmacm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fmacs(env, dst, src0, src1);
    fmacs(env, dst + 1, src0 + 1, src1 + 1);
}

static void fmscm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fmscs(env, dst, src0, src1);
    fmscs(env, dst + 1, src0 + 1, src1 + 1);
}

static void fnmacm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fnmacs(env, dst, src0, src1);
    fnmacs(env, dst + 1, src0 + 1, src1 + 1);
}

static void fnmscm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fnmscs(env, dst, src0, src1);
    fnmscs(env, dst + 1, src0 + 1, src1 + 1);
}

static void fnmulm(CPUCSKYState *env, int dst, int src0, int src1)
{
    fnmuls(env, dst, src0, src1);
    fnmuls(env, dst + 1, src0 + 1, src1 + 1);
}

void helper_cpwir(CPUCSKYState *env)
{
    unsigned int insn, op1, op2, op3, src0, src1, dst, round_mode;

    insn = env->cp1.fir;
    op1 = (insn >> 15) & 0x3f;
    op2 = (insn >> 10) & 0x1f;
    op3 = (insn >> 5)  & 0x1f;

    switch (op1) {
    case 0x0:
        switch (op2) {
        case 0x1:
            src0 = insn & 0x1f;
            switch (op3) {
            case 0x0:/* fcmpzhsd */
                fcmpzhsd(env, src0);
                break;
            case 0x4:/* fcmpzltd */
                fcmpzltd(env, src0);
                break;
            case 0x8:/* fcmpzned */
                fcmpzned(env, src0);
                break;
            case 0xc:/* fcmpzuod */
                fcmpzuod(env, src0);
                break;
            case 0x10:/* fcmpzhss */
                fcmpzhss(env, src0);
                break;
            case 0x14:/* fcmpzlts */
                fcmpzlts(env, src0);
                break;
            case 0x18:/* fcmpznes */
                fcmpznes(env, src0);
                break;
            case 0x1c:/* fcmpzuos */
                fcmpzuos(env, src0);
                break;
            default:
                goto wrong;
                break;
            }
            break;
        case 0x2:/* fcmphsd */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmphsd(env, src0, src1);
            break;
        case 0x3:/* fcmpltd */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmpltd(env, src0, src1);
            break;
        case 0x4:/* fcmpned */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmpned(env, src0, src1);
            break;
        case 0x5:/* fcmpuod */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmpuod(env, src0, src1);
            break;
        case 0x6:/* fcmphss */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmphss(env, src0, src1);
            break;
        case 0x7:/* fcmplts */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmplts(env, src0, src1);
            break;
        case 0x8:/* fcmpnes */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmpnes(env, src0, src1);
            break;
        case 0x9:/* fcmpuos */
            src0 = insn & 0x1f;
            src1 = (insn >> 5) & 0x1f;
            fcmpuos(env, src0, src1);
            break;
        case 0xa:/* fstod */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fstod(env, dst, src0);
            break;
        case 0xb:/* fdtos */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fdtos(env, dst, src0);
            break;
        case 0xc:/* fsitod */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fsitod(env, dst, src0);
            break;
        case 0xd:/* fsitos */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fsitos(env, dst, src0);
            break;
        case 0xe:/* fuitod */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fuitod(env, dst, src0);
            break;
        case 0xf:/* fuitos */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fuitos(env, dst, src0);
            break;
        case 0x10:/* fabsd */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fabsd(env, dst, src0);
            break;
        case 0x11:/* fabss */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fabss(env, dst, src0);
            break;
        case 0x12:/* fnegd */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fnegd(env, dst, src0);
            break;
        case 0x13:/* fnegs */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fnegs(env, dst, src0);
            break;
        case 0x14:/* fsqrtd */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fsqrtd(env, dst, src0);
            break;
        case 0x15:/* fsqrts */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fsqrts(env, dst, src0);
            break;
        case 0x16:/* frecipd */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            frecipd(env, dst, src0);
            break;
        case 0x17:/* frecips */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            frecips(env, dst, src0);
            break;
        case 0x18:/* fabsm */
            src0 = insn & 0x1e;
            dst = (insn >> 5) & 0x1e;
            fabsm(env, dst, src0);
            break;
        case 0x19:/* fnegm */
            src0 = insn & 0x1e;
            dst = (insn >> 5) & 0x1e;
            fnegm(env, dst, src0);
            break;
        case 0x1a:/* fmovd */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fmovd(env, dst, src0);
            break;
        case 0x1b:/* fmovs */
            src0 = insn & 0x1f;
            dst = (insn >> 5) & 0x1f;
            fmovs(env, dst, src0);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x1:/* fdtosi */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        round_mode = (insn >> 13) & 0x3;
        switch (round_mode) {
        case 0x0:
            fdtosirn(env, dst, src0);
            break;
        case 0x1:
            fdtosirz(env, dst, src0);
            break;
        case 0x2:
            fdtosirpi(env, dst, src0);
            break;
        case 0x3:
            fdtosirni(env, dst, src0);
            break;
        }
        break;
    case 0x2:/* fstosi */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        round_mode = (insn >> 13) & 0x3;
        switch (round_mode) {
        case 0x0:
            fstosirn(env, dst, src0);
            break;
        case 0x1:
            fstosirz(env, dst, src0);
            break;
        case 0x2:
            fstosirpi(env, dst, src0);
            break;
        case 0x3:
            fstosirni(env, dst, src0);
            break;
        }
        break;
    case 0x3:/* fdtoui */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        round_mode = (insn >> 13) & 0x3;
        switch (round_mode) {
        case 0x0:
            fdtouirn(env, dst, src0);
            break;
        case 0x1:
            fdtouirz(env, dst, src0);
            break;
        case 0x2:
            fdtouirpi(env, dst, src0);
            break;
        case 0x3:
            fdtouirni(env, dst, src0);
            break;
        }
        break;
    case 0x4:/* fstoui */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        round_mode = (insn >> 13) & 0x3;
        switch (round_mode) {
        case 0x0:
            fstouirn(env, dst, src0);
            break;
        case 0x1:
            fstouirz(env, dst, src0);
            break;
        case 0x2:
            fstouirpi(env, dst, src0);
            break;
        case 0x3:
            fstouirni(env, dst, src0);
            break;
        }
        break;
    case 0x6:/* faddd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        faddd(env, dst, src0, src1);
        break;
    case 0x7:/* fadds */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fadds(env, dst, src0, src1);
        break;
    case 0x8:/* fsubd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fsubd(env, dst, src0, src1);
        break;
    case 0x9:/* fsubs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fsubs(env, dst, src0, src1);
        break;
    case 0xa:/* fmacd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmacd(env, dst, src0, src1);
        break;
    case 0xb:/* fmacs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmacs(env, dst, src0, src1);
        break;
    case 0xc:/* fmscd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmscd(env, dst, src0, src1);
        break;
    case 0xd:/* fmscs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmscs(env, dst, src0, src1);
        break;
    case 0xe:/* fmuld */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmuld(env, dst, src0, src1);
        break;
    case 0xf:/* fmuls */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fmuls(env, dst, src0, src1);
        break;
    case 0x10:/* fdivd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fdivd(env, dst, src0, src1);
        break;
    case 0x11:/* fdivs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fdivs(env, dst, src0, src1);
        break;
    case 0x12:/* fnmacd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmacd(env, dst, src0, src1);
        break;
    case 0x13:/* fnmacs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmacs(env, dst, src0, src1);
        break;
    case 0x14:/* fnmscd */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmscd(env, dst, src0, src1);
        break;
    case 0x15:/* fnmscs */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmscs(env, dst, src0, src1);
        break;
    case 0x16:/* fnmuld */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmuld(env, dst, src0, src1);
        break;
    case 0x17:/* fnmuls */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1f;
        src1 = (insn >> 10) & 0x1f;
        fnmuls(env, dst, src0, src1);
        break;
    case 0x18:/* faddm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        faddm(env, dst, src0, src1);
        break;
    case 0x19:/* fsubm */
        src0 = insn & 0x1f;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fsubm(env, dst, src0, src1);
        break;
    case 0x1a:/* fmulm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fmulm(env, dst, src0, src1);
        break;
    case 0x1b:/* fmacm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fmacm(env, dst, src0, src1);
        break;
    case 0x1c:/* fmscm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fmscm(env, dst, src0, src1);
        break;
    case 0x1d:/* fnmacm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fnmacm(env, dst, src0, src1);
        break;
    case 0x1e:/* fnmscm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fnmscm(env, dst, src0, src1);
        break;
    case 0x1f:/* fnmulm */
        src0 = insn & 0x1e;
        dst = (insn >> 5) & 0x1e;
        src1 = (insn >> 10) & 0x1e;
        fnmulm(env, dst, src0, src1);
        break;
    default:
        wrong:  printf("wrong fpu insn: %d", insn);
        break;
    }

}

#if !defined(CONFIG_USER_ONLY)
uint32_t helper_mfcr_cr0(CPUCSKYState *env)
{
    env->cp0.psr &= ~0x8000c001;
    env->cp0.psr |= env->psr_s << 31;
    env->cp0.psr |= env->psr_c;
    env->cp0.psr |= env->psr_tm << 14;

    return env->cp0.psr;
}

uint32_t helper_mfcr_cr20(CPUCSKYState *env)
{
    return env->cp0.pacr[env->cp0.prsr & 0x7];
}

void helper_mtcr_cr0(CPUCSKYState *env, uint32_t rx)
{
    if ((env->cp0.psr & 0x2) != (rx & 0x2)) {
        helper_switch_regs(env);
    }
    env->cp0.psr = rx;
    env->psr_s = PSR_S(rx);
    env->psr_c = PSR_C(rx);
    env->psr_tm = PSR_TM(rx);
}

void helper_mtcr_cr18(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    if ((env->cp0.ccr & 0x1) != (rx & 0x1)) {
        /* flush global QEMU TLB and tb_jmp_cache */
        tlb_flush(cs);

        if (rx & 0x1) { /* for mmu/mgu */
            if (env->features & CSKY_MMU) {
                env->tlb_context->get_physical_address =
                                mmu_get_physical_address;
            } else if (env->features & CSKY_MGU) {
                env->tlb_context->get_physical_address =
                                mgu_get_physical_address;
            }
        } else {
            env->tlb_context->get_physical_address =
                          nommu_get_physical_address;
        }

    }

    env->cp0.ccr = rx;
}

void helper_mtcr_cr20(CPUCSKYState *env, uint32_t rx)
{
    env->cp0.pacr[env->cp0.prsr & 0x7] = rx;
}

void helper_meh_write(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    /* if ASID is Changed, QEMU TLB must be flush */
    if ((env->mmu.meh & 0xff) != (rx & 0xff)) {
        tlb_flush(cs);
    }

    env->mmu.meh = rx;
}

void helper_mcir_write(CPUCSKYState *env, uint32_t rx)
{
    /* Note: when more than one of the most significant
       bits are asserted at the same time, these operations
       are implemented according to the priority as follows:
           Tlb invalid all operation
           Tlb invalid operation
           Tlb probe operation
           Tlb writing index operation
           Tlb writing random operation
           Tlb reading operation
    */

    if (rx & CSKY_MCIR_TLBINV_MASK) {
        helper_tlbinv(env);
    } else if (rx & CSKY_MCIR_TLBP_MASK) {
        env->tlb_context->helper_tlbp(env);
    } else if (rx & CSKY_MCIR_TLBWI_MASK) {
        env->tlb_context->helper_tlbwi(env);
    } else if (rx & CSKY_MCIR_TLBWR_MASK) {
        env->tlb_context->helper_tlbwr(env);
    } else if (rx & CSKY_MCIR_TLBR_MASK) {
        env->tlb_context->helper_tlbr(env);
    }
}

#endif

