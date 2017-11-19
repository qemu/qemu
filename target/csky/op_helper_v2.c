/*
 *  CSKY helper routines
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

uint32_t helper_xsr(CPUCSKYState *env, uint32_t a, uint32_t b)
{
     uint64_t t0, t1;
     uint32_t n0, n1;

     t0 = a;
     t1 = t0 << 32 | env->psr_c << 31;
     t1 >>= b;
     n0 = t1 & 0xffffffff;
     n1 = (t1 >> 32) & 0xffffffff;
     env->psr_c = (n0 >> 31) & 0x1;
     return (n0 << 1) | n1;
}

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

uint32_t helper_ff0(uint32_t a)
{
    a =  ~a;
    if (a == 0) {
        return 32;
    } else {
        return __builtin_clz(a);
    }
}

/* VFP support.  We follow the convention used for VFP instrunctions:
   Single precition routines have a "s" suffix, double precision a
   "d" suffix.  */

void HELPER(vfp_update_fcr)(CPUCSKYState *env)
{
    if ((env->vfp.fcr >> 27) & 0x1) {
        env->vfp.fp_status.flush_inputs_to_zero = 0;
    } else {
        env->vfp.fp_status.flush_inputs_to_zero = 1;
    }
    switch ((env->vfp.fcr >> 24) & 0x3) {
    case 0:
        env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
        break;
    case 1:
        env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
        break;
    case 2:
        env->vfp.fp_status.float_rounding_mode = float_round_up;
        break;
    case 3:
        env->vfp.fp_status.float_rounding_mode = float_round_down;
        break;
    default:
        break;
    }
}

void HELPER(vfp_check_exception)(CPUCSKYState *env)
{
    env->vfp.fesr &= 0xffffff00;
    if (env->vfp.fp_status.float_exception_flags & float_flag_invalid) {
        if (env->vfp.fcr & 0x1) {
            env->vfp.fesr |= 0x81;
        } else {
            env->vfp.fesr |= 0x8181;
            /* FIXME: if IOE(FCR[0]) = 0, return qNaN
             * or Special Value to dest register. */
        }
    }
    if (env->vfp.fp_status.float_exception_flags & float_flag_divbyzero) {
        if (env->vfp.fcr & 0x2) {
            env->vfp.fesr |= 0x82;
        } else {
            env->vfp.fesr |= 0x8282;
            /* FIXME: return a signed INF to dest register. */
        }
    }
    if (env->vfp.fp_status.float_exception_flags & float_flag_overflow) {
        if (env->vfp.fcr & 0x4) {
            env->vfp.fesr |= 0x94;
        } else {
            env->vfp.fesr |= 0x9494;
            /* FIXME: According to RM(fcr[25:24]), roundoff the result. */
        }
    }
    if (env->vfp.fp_status.float_exception_flags & float_flag_underflow) {
        if (env->vfp.fcr & 0x8) {
            env->vfp.fesr |= 0x98;
        } else {
            env->vfp.fesr |= 0x9898;
            /* FIXME: According to FM(fcr[27]), return zero or
             * Minimum finite Number to dest register. */
        }
    }
    if (env->vfp.fp_status.float_exception_flags & float_flag_inexact) {
        if (env->vfp.fcr & 0x10) {
            env->vfp.fesr |= 0x90;
        } else {
            env->vfp.fesr |= 0x9090;
            /* FIXME: According to RM(fcr[25:24]), return a signed INF or
             * Maximum finite Number to dest register. */
        }
    }
    if (env->vfp.fp_status.float_exception_flags & float_flag_input_denormal) {
        if (env->vfp.fcr & 0x20) {
            env->vfp.fesr |= 0xa0;
        } else {
            env->vfp.fesr |= 0xa0a0;
            /* FIXME: According to FM(fcr[27]), flush the denormalized number
             * to Minimum finite Number or zero, do the instruction again. */
        }
    }
    env->vfp.fp_status.float_exception_flags = 0;
    /* if exception is enable, throw an exception. */
    if ((env->vfp.fcr & env->vfp.fesr) & 0x3f) {
        helper_exception(env, EXCP_CSKY_FLOAT);
    }
}

#define VFP_HELPER(name, p) HELPER(glue(glue(vfp_, name), p))

float32 VFP_HELPER(add, s)(float32 a, float32 b, CPUCSKYState *env)
{
    return float32_add(a, b , &env->vfp.fp_status);
}

float64 VFP_HELPER(add, d)(float64 a, float64 b, CPUCSKYState *env)
{
    return float64_add(a, b , &env->vfp.fp_status);
}

float32 VFP_HELPER(sub, s)(float32 a, float32 b, CPUCSKYState *env)
{
    return float32_sub(a, b , &env->vfp.fp_status);
}

float64 VFP_HELPER(sub, d)(float64 a, float64 b, CPUCSKYState *env)
{
    return float64_sub(a, b , &env->vfp.fp_status);
}

float32 VFP_HELPER(mul, s)(float32 a, float32 b, CPUCSKYState *env)
{
    return float32_mul(a, b , &env->vfp.fp_status);
}

float64 VFP_HELPER(mul, d)(float64 a, float64 b, CPUCSKYState *env)
{
    return float64_mul(a, b , &env->vfp.fp_status);
}


float32 VFP_HELPER(div, s)(float32 a, float32 b, CPUCSKYState *env)
{
    return float32_div(a, b , &env->vfp.fp_status);
}

float64 VFP_HELPER(div, d)(float64 a, float64 b, CPUCSKYState *env)
{
    return float64_div(a, b , &env->vfp.fp_status);
}

float32 VFP_HELPER(neg, s)(float32 a)
{
    return float32_chs(a);
}

float64 VFP_HELPER(neg, d)(float64 a)
{
    return float64_chs(a);
}

float32 VFP_HELPER(abs, s)(float32 a)
{
    return float32_abs(a);
}

float64 VFP_HELPER(abs, d)(float64 a)
{
    return float64_abs(a);
}

float32 VFP_HELPER(sqrt, s)(float32 a, CPUCSKYState *env)
{
    return float32_sqrt(a, &env->vfp.fp_status);
}

float64 VFP_HELPER(sqrt, d)(float64 a, CPUCSKYState *env)
{
    return float64_sqrt(a, &env->vfp.fp_status);
}

float32 VFP_HELPER(recip, s)(float32 a, CPUCSKYState *env)
{
    float_status *s = &env->vfp.fp_status;
    float32 one = int32_to_float32(1, s);
    return float32_div(one, a, s);
}

float64 VFP_HELPER(recip, d)(float64 a, CPUCSKYState *env)
{
    float_status *s = &env->vfp.fp_status;
    float64 one = int32_to_float64(1, s);
    return float64_div(one, a, s);
}

void VFP_HELPER(cmp_ge, s)(float32 a, float32 b, CPUCSKYState *env)
{
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_ge, d)(float64 a, float64 b, CPUCSKYState *env)
{
    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_l, s)(float32 a, float32 b, CPUCSKYState *env)
{
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_l, d)(float64 a, float64 b, CPUCSKYState *env)
{
    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_ls, s)(float32 a, float32 b, CPUCSKYState *env)
{
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_ls, d)(float64 a, float64 b, CPUCSKYState *env)
{
    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void VFP_HELPER(cmp_ne, s)(float32 a, float32 b, CPUCSKYState *env)
{
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void VFP_HELPER(cmp_ne, d)(float64 a, float64 b, CPUCSKYState *env)
{
    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}


void VFP_HELPER(cmp_isNAN, s)(float32 a, float32 b, CPUCSKYState *env)
{
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void VFP_HELPER(cmp_isNAN, d)(float64 a,  float64 b, CPUCSKYState *env)
{
    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
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

float32 VFP_HELPER(fmovi, s)(uint32_t imm, uint32_t pos, uint32_t aSign,
                              CPUCSKYState *env)
{
    int32_t a;
    uint32_t aSig, aExp;
    if (aSign == 0) {
        a = imm;
    } else {
        a = ~imm + 1;
    }

    float32 tmp = int32_to_float32(a, &env->vfp.fp_status);
    aSig = float32_val(tmp) & 0x007FFFFF;
    aExp = ((float32_val(tmp) >> 23) & 0xFF) - pos;
    return (aSign << 31) | aSig | (aExp << 23);
}

float64 VFP_HELPER(fmovi, d)(uint32_t imm, uint32_t pos, uint32_t aSign,
                              CPUCSKYState *env)
{
    int64_t a;
    uint64_t aSig, aExp;
    if (aSign == 0) {
        a = imm;
    } else {
        a = ~imm + 1;
    }

    float64 tmp = int32_to_float64(a, &env->vfp.fp_status);
    aSig = float64_val(tmp) & 0x000FFFFFFFFFFFFF;
    aExp = ((float64_val(tmp) >> 52) & 0x7FF) - pos;
    return ((uint64_t)aSign << 63) | aSig | (aExp << 52);
}

float32 VFP_HELPER(tosirn, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_int32(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(tosirz, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_int32_round_to_zero(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(tosirpi, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((float *)&x) > 0) {
        return vfp_itos(float32_to_int32_round_to_zero
                        (x, &env->vfp.fp_status) + 1);
    } else {
        return vfp_itos(float32_to_int32_round_to_zero(x, &env->vfp.fp_status));
    }
}

float32 VFP_HELPER(tosirni, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((float *)&x) > 0) {
        return vfp_itos(float32_to_int32_round_to_zero(x, &env->vfp.fp_status));
    } else {
        return vfp_itos(float32_to_int32_round_to_zero
                        (x, &env->vfp.fp_status) - 1);
    }
}

float32 VFP_HELPER(tosirn, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_int32(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(tosirz, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float64_to_int32_round_to_zero(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(tosirpi, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((double *)&x) > 0) {
        return vfp_itos(float64_to_int32_round_to_zero
                        (x, &env->vfp.fp_status) + 1);
    } else {
        return vfp_itos(float64_to_int32_round_to_zero(x, &env->vfp.fp_status));
    }
}

float32 VFP_HELPER(tosirni, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((double *)&x) > 0) {
        return vfp_itos(float64_to_int32_round_to_zero
                        (x, &env->vfp.fp_status));
    } else {
        return vfp_itos(float64_to_int32_round_to_zero
                        (x, &env->vfp.fp_status) - 1);
    }
}


float32 VFP_HELPER(touirn, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_uint32(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(touirz, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    return vfp_itos(float32_to_uint32_round_to_zero(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(touirpi, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((float *)&x) > 0) {
        return vfp_itos(float32_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status) + 1);
    } else {
        return vfp_itos(float32_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status));
    }
}

float32 VFP_HELPER(touirni, s)(float32 x, CPUCSKYState *env)
{
    if (float32_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((float *)&x) > 0) {
        return vfp_itos(float32_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status));
    } else {
        return vfp_itos(float32_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status) - 1);
    }
}

float32 VFP_HELPER(touirn, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }

    return vfp_itos(float64_to_uint32(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(touirz, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }

    return vfp_itos(float64_to_uint32_round_to_zero(x, &env->vfp.fp_status));
}

float32 VFP_HELPER(touirpi, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((double *)&x) > 0) {
        return vfp_itos(float64_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status) + 1);
    } else {
        return vfp_itos(float64_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status));
    }
}

float32 VFP_HELPER(touirni, d)(float64 x, CPUCSKYState *env)
{
    if (float64_is_any_nan(x)) {
        return float32_zero;
    }
    if (*((double *)&x) > 0) {
        return vfp_itos(float64_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status));
    } else {
        return vfp_itos(float64_to_uint32_round_to_zero
                        (x, &env->vfp.fp_status) - 1);
    }
}



/* Integer to float conversion.  */
float32 VFP_HELPER(uito, s)(float32 x, CPUCSKYState *env)
{
    return uint32_to_float32(vfp_stoi(x), &env->vfp.fp_status);
}

float64 VFP_HELPER(uito, d)(float32 x, CPUCSKYState *env)
{
    return uint32_to_float64(vfp_stoi(x), &env->vfp.fp_status);
}

float32 VFP_HELPER(sito, s)(float32 x, CPUCSKYState *env)
{
    return int32_to_float32(vfp_stoi(x), &env->vfp.fp_status);
}

float64 VFP_HELPER(sito, d)(float32 x, CPUCSKYState *env)
{
    return int32_to_float64(vfp_stoi(x), &env->vfp.fp_status);
}



/* floating point conversion */
float64 VFP_HELPER(tod, s)(float32 x, CPUCSKYState *env)
{
    float64 r = float32_to_float64(x, &env->vfp.fp_status);
    /* ARM requires that S<->D conversion of any kind of NaN generates
     * a quiet NaN by forcing the most significant frac bit to 1.
     */
    return float64_maybe_silence_nan(r, &env->vfp.fp_status);
}

float32 VFP_HELPER(tos, d)(float64 x, CPUCSKYState *env)
{
    float32 r =  float64_to_float32(x, &env->vfp.fp_status);
    /* ARM requires that S<->D conversion of any kind of NaN generates
     * a quiet NaN by forcing the most significant frac bit to 1.
     */
    return float32_maybe_silence_nan(r, &env->vfp.fp_status);
}

void VDSP_HELPER(store)(uint32_t rz, uint64_t tmp1, CPUCSKYState *env)
{
    env->vfp.reg[rz].udspl[0] = tmp1;
}

void VDSP_HELPER(store2)(uint32_t rz, uint64_t tmp1, uint64_t tmp2,
                         CPUCSKYState *env)
{
    env->vfp.reg[rz].udspl[0] = tmp1;
    env->vfp.reg[rz].udspl[1] = tmp2;
}

#ifndef CONFIG_USER_ONLY
uint32_t helper_mfcr_cr0(CPUCSKYState *env)
{
    helper_update_psr(env);
    return env->cp0.psr;
}

void helper_mtcr_cr0(CPUCSKYState *env, uint32_t rx)
{
    if (!(env->features & ABIV2_JAVA)) {
        rx &= ~0x400;
    }

    helper_save_sp(env);
    if (env->features & ABIV2_TEE) {
        helper_tee_save_cr(env);
        /* save psr */
        if (env->psr_t) {
            env->tee.t_psr = env->cp0.psr;
        } else {
            env->tee.nt_psr = env->cp0.psr;
        }

        env->cp0.psr = rx;
        helper_record_psr_bits(env);
        helper_tee_choose_cr(env);
    } else {
        env->cp0.psr = rx;
        helper_record_psr_bits(env);
    }

    if ((env->cp0.psr & 0x2) != (rx & 0x2)) {
        if (env->features & (CPU_610 | CPU_807 | CPU_810)) {
            helper_switch_regs(env);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Only CK610 CK807 CK810 have alternative registers");
        }
    }
    helper_choose_sp(env);
}

void helper_mtcr_cr14(CPUCSKYState *env, uint32_t rx)
{
    env->stackpoint.nt_usp = rx;
}

uint32_t helper_mfcr_cr14(CPUCSKYState *env)
{
    return env->stackpoint.nt_usp;
}

void helper_mtcr_cr17(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    env->cp0.cfr = rx;

    /* Invaild inst cache */
    if ((rx & 0x1) && (rx & 0x10)) {
        tb_flush(cs);
        cpu_loop_exit(cs);
    }
}

void helper_mtcr_cr18(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));

    if ((env->cp0.ccr & 0x1) != (rx & 0x1)) {
        /* flush global QEMU TLB and tb_jmp_cache */
        tlb_flush(cs);

        if (rx & 0x1) {  /* enable mmu/mgu */
            if (env->features & CSKY_MMU)  {
                env->tlb_context->get_physical_address =
                    mmu_get_physical_address;
            } else {
                env->tlb_context->get_physical_address =
                    mgu_get_physical_address;
            }
        } else {
            env->tlb_context->get_physical_address = nommu_get_physical_address;
        }
    }

    env->cp0.ccr = rx;
}

void helper_psrclr(CPUCSKYState *env, uint32_t imm)
{
    /* AF bit */
    if ((imm & 0x1)  && (env->cp0.psr & 0x2)) {
        env->cp0.psr &= ~0x2;
        helper_switch_regs(env);
    }
    /* FE bit */
    if (imm & 0x2) {
        env->cp0.psr &= ~0x10;
    }
    /* IE bit */
    if (imm & 0x4) {
        env->cp0.psr &= ~0x40;
    }
    /* EE bit */
    if (imm & 0x8) {
        env->cp0.psr &= ~0x100;
    }
}


void helper_psrset(CPUCSKYState *env, uint32_t imm)
{
    /* AF bit */
    if ((imm & 0x1) && !(env->cp0.psr & 0x2)) {
        env->cp0.psr |= 0x2;
        helper_switch_regs(env);
    }
    /* FE bit */
    if (imm & 0x2) {
        env->cp0.psr |= 0x10;
    }
    /* IE bit */
    if (imm & 0x4) {
        env->cp0.psr |= 0x40;
    }
    /* EE bit */
    if (imm & 0x8) {
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

void helper_wsc(CPUCSKYState *env)
{
    helper_save_sp(env);
    helper_tee_save_cr(env);
    if (env->psr_t) {
        /* Secured world switch to Non-secured world. */
        cpu_stl_data(env, env->stackpoint.t_ssp - 4, env->pc + 4);
        cpu_stl_data(env, env->stackpoint.t_ssp - 8, env->cp0.psr);
        env->stackpoint.t_ssp -= 8;

        env->tee.t_psr = env->cp0.psr;
        env->tee.t_psr &= ~PSR_HS_MASK;
        env->tee.nt_psr |= PSR_SP_MASK;
        env->tee.nt_psr &= ~PSR_VEC_MASK;
        env->tee.nt_psr |= (env->tee.t_psr & PSR_VEC_MASK);
        env->tee.nt_psr |= PSR_S_MASK;
        env->cp0.psr = env->tee.nt_psr;
        env->pc = cpu_ldl_code(env, env->tee.nt_ebr + 0 * 4);
    } else {
        /* Non-secured world switch to secured world. */
        cpu_stl_data(env, env->stackpoint.nt_ssp - 4, env->pc + 4);
        cpu_stl_data(env, env->stackpoint.nt_ssp - 8, env->cp0.psr);
        env->stackpoint.nt_ssp -= 8;

        env->tee.nt_psr = env->cp0.psr;
        env->tee.t_psr |= PSR_SC_MASK;
        env->tee.t_psr &= ~PSR_SP_MASK;
        env->tee.t_psr &= ~PSR_VEC_MASK;
        env->tee.t_psr |= (env->tee.nt_psr & PSR_VEC_MASK);
        env->tee.t_psr |= PSR_S_MASK;
        env->cp0.psr = env->tee.t_psr;
        env->pc = cpu_ldl_code(env, env->tee.t_ebr + 0 * 4);
    }

    if ((env->tee.nt_psr & 0x2) != (env->tee.t_psr & 0x2)) {
        helper_switch_regs(env);
    }
    helper_record_psr_bits(env);
    helper_tee_choose_cr(env);
    helper_choose_sp(env);
    env->sce_condexec_bits = env->sce_condexec_bits_bk;
}

static inline uint32_t do_helper_tee_rte(CPUCSKYState *env)
{
    uint32_t tmp = 0;
    uint32_t is_from_wsc = TRUE;
    helper_save_sp(env);
    helper_tee_save_cr(env);

    if (env->psr_t == 1
        && PSR_SP(env->cp0.psr) == 0
        && PSR_SC(env->tee.t_psr) == 1) {
        /* return from Trust world to Non-Trust world, wsc */
        tmp = env->tee.t_psr;
        env->tee.t_psr = env->cp0.psr;
        env->cp0.psr = cpu_ldl_data(env, env->stackpoint.nt_ssp);
        env->pc = cpu_ldl_data(env, env->stackpoint.nt_ssp + 4);
        env->stackpoint.nt_ssp += 8;
    } else if (env->psr_t == 0
               && PSR_SP(env->cp0.psr) == 1
               && PSR_HS(env->tee.t_psr) == 0) {
        /* return from Non-Trust world to Trust world, wsc */
        tmp = env->tee.nt_psr;
        env->tee.nt_psr = env->cp0.psr;
        env->cp0.psr = cpu_ldl_data(env, env->stackpoint.t_ssp);
        env->pc = cpu_ldl_data(env, env->stackpoint.t_ssp + 4);
        env->stackpoint.t_ssp += 8;
    } else if (env->psr_t == 1
               && PSR_SP(env->cp0.psr) == 0
               && PSR_SC(env->tee.t_psr) == 0) {
        /* return from Trust world to Non-Trust world, interrupt */
        is_from_wsc = FALSE;
        tmp = env->cp0.psr;
        env->tee.t_psr = env->tee.t_epsr;
        env->cp0.psr = cpu_ldl_data(env, env->stackpoint.nt_ssp);
        env->pc = cpu_ldl_data(env, env->stackpoint.nt_ssp + 4);
        env->stackpoint.nt_ssp += 8;
    } else if (env->psr_t == 0
               && PSR_SP(env->cp0.psr) == 1
               && PSR_HS(env->tee.t_psr) == 1) {
        /* return from Non-Trust world to Trust world, interrupt */
        is_from_wsc = FALSE;
        tmp = env->cp0.psr;
        env->tee.nt_psr = env->tee.nt_epsr;
        helper_tee_restore_gpr(env);
        env->cp0.psr = cpu_ldl_data(env, env->stackpoint.t_ssp);
        env->pc = cpu_ldl_data(env, env->stackpoint.t_ssp + 4);
        env->stackpoint.t_ssp += 8;
    } else {
        /* return from interrupt without change the world, interrupt */
        is_from_wsc = FALSE;
        tmp = env->cp0.psr;
        env->cp0.psr = env->cp0.epsr;
        env->pc = env->cp0.epc;
    }

    if ((tmp & 0x2) != (env->cp0.psr & 0x2)) {
        helper_switch_regs(env);
    }
    helper_record_psr_bits(env);
    helper_tee_choose_cr(env);
    helper_choose_sp(env);
    return is_from_wsc;
}

/* helper for rte and nir */
void helper_rte(CPUCSKYState *env)
{
    uint32_t irq;
    uint32_t is_from_wsc;
    if (env->features & ABIV2_TEE) {
        is_from_wsc = do_helper_tee_rte(env);
        if (is_from_wsc) {
            return;
        }
    } else {
        /* cpu without feature tee, or return from one world to
         * the same kind of world */
        if ((env->cp0.psr & 0x2) != (env->cp0.epsr & 0x2)) {
            helper_switch_regs(env);
        }
        helper_save_sp(env);
        env->cp0.psr = env->cp0.epsr;
        env->pc = env->cp0.epc;
        helper_record_psr_bits(env);
        helper_choose_sp(env);
    }

    /* if irq >= 32, it is a vic interrupt. */
    irq = env->intc_signals.isr & 0xff;
    if (irq >= 32) {
        env->intc_signals.isr &= ~0xff;
        env->intc_signals.isr |= ((env->cp0.epsr >> 16) & 0xff);
        env->intc_signals.iabr &= ~(1 << (irq - 32));
        /* if the finished irq is same as VIC_IPTR(threshold),
         *  clean the en bit of VIC_IPTR. */
        if (irq == ((env->intc_signals.iptr && 0xff00) >> 8)) {
            env->intc_signals.iptr &= ~(1 << 31);
        }
    }
    if (unlikely(PSR_TP(env->cp0.epsr))) {
        env->cp0.psr |= EXCP_CSKY_TRACE << 16;
        helper_update_psr(env);
        env->cp0.psr &= ~PSR_TP_MASK;   /* clear TP in EPSR */
        env->cp0.epsr = env->cp0.psr;

        env->cp0.psr |= PSR_S_MASK;
        env->cp0.psr &= ~PSR_TM_MASK;
        env->cp0.psr &= ~PSR_EE_MASK;
        env->cp0.psr &= ~PSR_IE_MASK;

        env->pc = cpu_ldl_code(env, env->cp0.vbr + EXCP_CSKY_TRACE * 4);
        if (unlikely((env->pc & 0x1) != ((env->cp0.psr & 0x2) >> 1))) {
            helper_switch_regs(env);
            env->cp0.psr &= ~0x1;
            env->cp0.psr |= (env->pc & 0x1) << 1;
        }
        helper_choose_sp(env);
        env->pc &= ~0x1;
    } else {
        env->sce_condexec_bits = env->sce_condexec_bits_bk;
    }
}

void helper_rfi(CPUCSKYState *env)
{
    if ((env->cp0.psr & 0x2) != (env->cp0.fpsr & 0x2)) {
        helper_switch_regs(env);
    }
    helper_save_sp(env);
    env->cp0.psr = env->cp0.fpsr & ~(0xff << 16);
    helper_record_psr_bits(env);
    helper_choose_sp(env);
    if (unlikely(PSR_TP(env->cp0.fpsr))) {
        env->cp0.psr |= EXCP_CSKY_TRACE << 16;
        helper_update_psr(env);
        env->cp0.psr &= ~PSR_TP_MASK;   /* clear TP in EPSR */
        env->cp0.epsr = env->cp0.psr;

        env->cp0.psr |= PSR_S_MASK;
        env->cp0.psr &= ~PSR_TM_MASK;
        env->cp0.psr &= ~PSR_EE_MASK;
        env->cp0.psr &= ~PSR_IE_MASK;

        env->pc = cpu_ldl_code(env, env->cp0.vbr + EXCP_CSKY_TRACE * 4);
        if (unlikely((env->pc & 0x1) != ((env->cp0.psr & 0x2) >> 1))) {
            helper_switch_regs(env);
            env->cp0.psr &= ~0x1;
            env->cp0.psr |= (env->pc & 0x1) << 1;
        }
        helper_choose_sp(env);
        env->pc &= ~0x1;
    } else {
        env->pc = env->cp0.fpc;
        env->sce_condexec_bits = env->sce_condexec_bits_bk;
    }
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
    if ((rx & CSKY_MCIR_TTLBINV_ALL_MASK) && (env->features & ABIV2_TEE)) {
        helper_ttlbinv_all(env);
    } else if (rx & CSKY_MCIR_TLBINV_ALL_MASK) {
        helper_tlbinv_all(env);
    } else if (rx & CSKY_MCIR_TLBINV_MASK) {
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

uint32_t helper_tee_mfcr_cr19(CPUCSKYState *env)
{
    uint32_t i, s7_s0;
    uint32_t res = 0;
    if (env->psr_t) {
        res = env->cp0.capr;
    } else {
        /* Non-Trust, S bit can not read or write,
         * NX and SAP, can read and write only if S bit is 1. */
        s7_s0 = env->cp0.capr >> 24;
        /* Get NX */
        res = env->cp0.capr & s7_s0;
        /* Get SAP */
        for (i = 0; i < 8; i++) {
            if (s7_s0 & (1 << i)) {
                res |= (env->cp0.capr & (0x3 << (2 * i + 8)));
            }
        }
    }
    return res;
}

void helper_tee_mtcr_cr19(CPUCSKYState *env, uint32_t rx)
{
    uint32_t i, s7_s0, mask;
    if (env->psr_t) {
        env->cp0.capr = rx;
    } else {
        /* Non-Trust, S bit can not read or write,
         * NX and SAP, can read and write only if S bit is 1. */
        s7_s0 = env->cp0.capr >> 24;
        /* Set NX and SAP */
        for (i = 0; i < 8; i++) {
            if (s7_s0 & (1 << i)) {
                mask = (0x3 << (2 * i + 8)) | (0x1 << i);
                env->cp0.capr &= ~mask;
                env->cp0.capr |= rx & mask;
            }
        }
    }
}

uint32_t helper_mfcr_cr20(CPUCSKYState *env)
{
    uint32_t rid;
    rid = env->cp0.prsr & 0x7;

    if (!(env->features & ABIV2_TEE)
        || env->psr_t
        || env->cp0.capr & (1 << (rid + 24))) {
        return env->cp0.pacr[rid];
    } else {
        return 0;
    }
}

void helper_mtcr_cr20(CPUCSKYState *env, uint32_t rx)
{
    uint32_t rid;
    rid = env->cp0.prsr & 0x7;

    if (!(env->features & ABIV2_TEE)
        || env->psr_t
        || env->cp0.capr & (1 << (rid + 24))) {
        env->cp0.pacr[rid] = rx;
    }
}

uint32_t helper_mfcr_cpidr(CPUCSKYState *env)
{
    uint32_t counter;

    counter = env->cp0.cpidr_counter;
    env->cp0.cpidr_counter = (counter + 1) % 4;
    return env->cp0.cpidr[counter];
}
#endif /* !CONFIG_USER_ONLY */

