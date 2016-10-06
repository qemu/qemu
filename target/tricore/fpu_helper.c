/*
 *  TriCore emulation for qemu: fpu helper.
 *
 *  Copyright (c) 2016 Bastian Koppelmann University of Paderborn
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
#include "exec/helper-proto.h"

#define QUIET_NAN 0x7fc00000
#define ADD_NAN   0x7fc00001
#define DIV_NAN   0x7fc00008
#define MUL_NAN   0x7fc00002
#define FPU_FS PSW_USB_C
#define FPU_FI PSW_USB_V
#define FPU_FV PSW_USB_SV
#define FPU_FZ PSW_USB_AV
#define FPU_FU PSW_USB_SAV

/* we don't care about input_denormal */
static inline uint8_t f_get_excp_flags(CPUTriCoreState *env)
{
    return get_float_exception_flags(&env->fp_status)
           & (float_flag_invalid
              | float_flag_overflow
              | float_flag_underflow
              | float_flag_output_denormal
              | float_flag_divbyzero
              | float_flag_inexact);
}

static inline bool f_is_denormal(float32 arg)
{
    return float32_is_zero_or_denormal(arg) && !float32_is_zero(arg);
}

static inline float32 f_maddsub_nan_result(float32 arg1, float32 arg2,
                                           float32 arg3, float32 result,
                                           uint32_t muladd_negate_c)
{
    uint32_t aSign, bSign, cSign;
    uint32_t aExp, bExp, cExp;

    if (float32_is_any_nan(arg1) || float32_is_any_nan(arg2) ||
        float32_is_any_nan(arg3)) {
        return QUIET_NAN;
    } else if (float32_is_infinity(arg1) && float32_is_zero(arg2)) {
        return MUL_NAN;
    } else if (float32_is_zero(arg1) && float32_is_infinity(arg2)) {
        return MUL_NAN;
    } else {
        aSign = arg1 >> 31;
        bSign = arg2 >> 31;
        cSign = arg3 >> 31;

        aExp = (arg1 >> 23) & 0xff;
        bExp = (arg2 >> 23) & 0xff;
        cExp = (arg3 >> 23) & 0xff;

        if (muladd_negate_c) {
            cSign ^= 1;
        }
        if (((aExp == 0xff) || (bExp == 0xff)) && (cExp == 0xff)) {
            if (aSign ^ bSign ^ cSign) {
                return ADD_NAN;
            }
        }
    }

    return result;
}

static void f_update_psw_flags(CPUTriCoreState *env, uint8_t flags)
{
    uint8_t some_excp = 0;
    set_float_exception_flags(0, &env->fp_status);

    if (flags & float_flag_invalid) {
        env->FPU_FI = 1 << 31;
        some_excp = 1;
    }

    if (flags & float_flag_overflow) {
        env->FPU_FV = 1 << 31;
        some_excp = 1;
    }

    if (flags & float_flag_underflow || flags & float_flag_output_denormal) {
        env->FPU_FU = 1 << 31;
        some_excp = 1;
    }

    if (flags & float_flag_divbyzero) {
        env->FPU_FZ = 1 << 31;
        some_excp = 1;
    }

    if (flags & float_flag_inexact || flags & float_flag_output_denormal) {
        env->PSW |= 1 << 26;
        some_excp = 1;
    }

    env->FPU_FS = some_excp;
}

#define FADD_SUB(op)                                                           \
uint32_t helper_f##op(CPUTriCoreState *env, uint32_t r1, uint32_t r2)          \
{                                                                              \
    float32 arg1 = make_float32(r1);                                           \
    float32 arg2 = make_float32(r2);                                           \
    uint32_t flags;                                                            \
    float32 f_result;                                                          \
                                                                               \
    f_result = float32_##op(arg2, arg1, &env->fp_status);                      \
    flags = f_get_excp_flags(env);                                             \
    if (flags) {                                                               \
        /* If the output is a NaN, but the inputs aren't,                      \
           we return a unique value.  */                                       \
        if ((flags & float_flag_invalid)                                       \
            && !float32_is_any_nan(arg1)                                       \
            && !float32_is_any_nan(arg2)) {                                    \
            f_result = ADD_NAN;                                                \
        }                                                                      \
        f_update_psw_flags(env, flags);                                        \
    } else {                                                                   \
        env->FPU_FS = 0;                                                       \
    }                                                                          \
    return (uint32_t)f_result;                                                 \
}
FADD_SUB(add)
FADD_SUB(sub)

uint32_t helper_fmul(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint32_t flags;
    float32 arg1 = make_float32(r1);
    float32 arg2 = make_float32(r2);
    float32 f_result;

    f_result = float32_mul(arg1, arg2, &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        /* If the output is a NaN, but the inputs aren't,
           we return a unique value.  */
        if ((flags & float_flag_invalid)
            && !float32_is_any_nan(arg1)
            && !float32_is_any_nan(arg2)) {
                f_result = MUL_NAN;
        }
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return (uint32_t)f_result;

}

uint32_t helper_fdiv(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint32_t flags;
    float32 arg1 = make_float32(r1);
    float32 arg2 = make_float32(r2);
    float32 f_result;

    f_result = float32_div(arg1, arg2 , &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        /* If the output is a NaN, but the inputs aren't,
           we return a unique value.  */
        if ((flags & float_flag_invalid)
            && !float32_is_any_nan(arg1)
            && !float32_is_any_nan(arg2)) {
                f_result = DIV_NAN;
        }
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }

    return (uint32_t)f_result;
}

uint32_t helper_fmadd(CPUTriCoreState *env, uint32_t r1,
                      uint32_t r2, uint32_t r3)
{
    uint32_t flags;
    float32 arg1 = make_float32(r1);
    float32 arg2 = make_float32(r2);
    float32 arg3 = make_float32(r3);
    float32 f_result;

    f_result = float32_muladd(arg1, arg2, arg3, 0, &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        if (flags & float_flag_invalid) {
            arg1 = float32_squash_input_denormal(arg1, &env->fp_status);
            arg2 = float32_squash_input_denormal(arg2, &env->fp_status);
            arg3 = float32_squash_input_denormal(arg3, &env->fp_status);
            f_result = f_maddsub_nan_result(arg1, arg2, arg3, f_result, 0);
        }
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return (uint32_t)f_result;
}

uint32_t helper_fmsub(CPUTriCoreState *env, uint32_t r1,
                      uint32_t r2, uint32_t r3)
{
    uint32_t flags;
    float32 arg1 = make_float32(r1);
    float32 arg2 = make_float32(r2);
    float32 arg3 = make_float32(r3);
    float32 f_result;

    f_result = float32_muladd(arg1, arg2, arg3, float_muladd_negate_product,
                              &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        if (flags & float_flag_invalid) {
            arg1 = float32_squash_input_denormal(arg1, &env->fp_status);
            arg2 = float32_squash_input_denormal(arg2, &env->fp_status);
            arg3 = float32_squash_input_denormal(arg3, &env->fp_status);

            f_result = f_maddsub_nan_result(arg1, arg2, arg3, f_result, 1);
        }
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return (uint32_t)f_result;
}

uint32_t helper_fcmp(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint32_t result, flags;
    float32 arg1 = make_float32(r1);
    float32 arg2 = make_float32(r2);

    set_flush_inputs_to_zero(0, &env->fp_status);

    result = 1 << (float32_compare_quiet(arg1, arg2, &env->fp_status) + 1);
    result |= f_is_denormal(arg1) << 4;
    result |= f_is_denormal(arg2) << 5;

    flags = f_get_excp_flags(env);
    if (flags) {
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }

    set_flush_inputs_to_zero(1, &env->fp_status);
    return result;
}

uint32_t helper_ftoi(CPUTriCoreState *env, uint32_t arg)
{
    float32 f_arg = make_float32(arg);
    int32_t result, flags;

    result = float32_to_int32(f_arg, &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        if (float32_is_any_nan(f_arg)) {
            result = 0;
        }
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return (uint32_t)result;
}

uint32_t helper_itof(CPUTriCoreState *env, uint32_t arg)
{
    float32 f_result;
    uint32_t flags;
    f_result = int32_to_float32(arg, &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags) {
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return (uint32_t)f_result;
}

uint32_t helper_ftouz(CPUTriCoreState *env, uint32_t arg)
{
    float32 f_arg = make_float32(arg);
    uint32_t result;
    int32_t flags;

    result = float32_to_uint32_round_to_zero(f_arg, &env->fp_status);

    flags = f_get_excp_flags(env);
    if (flags & float_flag_invalid) {
        flags &= ~float_flag_inexact;
        if (float32_is_any_nan(f_arg)) {
            result = 0;
        }
    } else if (float32_lt_quiet(f_arg, 0, &env->fp_status)) {
        flags = float_flag_invalid;
        result = 0;
    }

    if (flags) {
        f_update_psw_flags(env, flags);
    } else {
        env->FPU_FS = 0;
    }
    return result;
}

void helper_updfl(CPUTriCoreState *env, uint32_t arg)
{
    env->FPU_FS =  extract32(arg, 7, 1) & extract32(arg, 15, 1);
    env->FPU_FI = (extract32(arg, 6, 1) & extract32(arg, 14, 1)) << 31;
    env->FPU_FV = (extract32(arg, 5, 1) & extract32(arg, 13, 1)) << 31;
    env->FPU_FZ = (extract32(arg, 4, 1) & extract32(arg, 12, 1)) << 31;
    env->FPU_FU = (extract32(arg, 3, 1) & extract32(arg, 11, 1)) << 31;
    /* clear FX and RM */
    env->PSW &= ~(extract32(arg, 10, 1) << 26);
    env->PSW |= (extract32(arg, 2, 1) & extract32(arg, 10, 1)) << 26;

    fpu_set_state(env);
}
