/*
 *  PowerPC floating point and SPE emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "helper.h"

/*****************************************************************************/
/* Floating point operations helpers */
uint64_t helper_float32_to_float64(CPUPPCState *env, uint32_t arg)
{
    CPU_FloatU f;
    CPU_DoubleU d;

    f.l = arg;
    d.d = float32_to_float64(f.f, &env->fp_status);
    return d.ll;
}

uint32_t helper_float64_to_float32(CPUPPCState *env, uint64_t arg)
{
    CPU_FloatU f;
    CPU_DoubleU d;

    d.ll = arg;
    f.f = float64_to_float32(d.d, &env->fp_status);
    return f.l;
}

static inline int isden(float64 d)
{
    CPU_DoubleU u;

    u.d = d;

    return ((u.ll >> 52) & 0x7FF) == 0;
}

static inline int ppc_float32_get_unbiased_exp(float32 f)
{
    return ((f >> 23) & 0xFF) - 127;
}

static inline int ppc_float64_get_unbiased_exp(float64 f)
{
    return ((f >> 52) & 0x7FF) - 1023;
}

uint32_t helper_compute_fprf(CPUPPCState *env, uint64_t arg, uint32_t set_fprf)
{
    CPU_DoubleU farg;
    int isneg;
    int ret;

    farg.ll = arg;
    isneg = float64_is_neg(farg.d);
    if (unlikely(float64_is_any_nan(farg.d))) {
        if (float64_is_signaling_nan(farg.d)) {
            /* Signaling NaN: flags are undefined */
            ret = 0x00;
        } else {
            /* Quiet NaN */
            ret = 0x11;
        }
    } else if (unlikely(float64_is_infinity(farg.d))) {
        /* +/- infinity */
        if (isneg) {
            ret = 0x09;
        } else {
            ret = 0x05;
        }
    } else {
        if (float64_is_zero(farg.d)) {
            /* +/- zero */
            if (isneg) {
                ret = 0x12;
            } else {
                ret = 0x02;
            }
        } else {
            if (isden(farg.d)) {
                /* Denormalized numbers */
                ret = 0x10;
            } else {
                /* Normalized numbers */
                ret = 0x00;
            }
            if (isneg) {
                ret |= 0x08;
            } else {
                ret |= 0x04;
            }
        }
    }
    if (set_fprf) {
        /* We update FPSCR_FPRF */
        env->fpscr &= ~(0x1F << FPSCR_FPRF);
        env->fpscr |= ret << FPSCR_FPRF;
    }
    /* We just need fpcc to update Rc1 */
    return ret & 0xF;
}

/* Floating-point invalid operations exception */
static inline uint64_t fload_invalid_op_excp(CPUPPCState *env, int op,
                                             int set_fpcc)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    uint64_t ret = 0;
    int ve;

    ve = fpscr_ve;
    switch (op) {
    case POWERPC_EXCP_FP_VXSNAN:
        env->fpscr |= 1 << FPSCR_VXSNAN;
        break;
    case POWERPC_EXCP_FP_VXSOFT:
        env->fpscr |= 1 << FPSCR_VXSOFT;
        break;
    case POWERPC_EXCP_FP_VXISI:
        /* Magnitude subtraction of infinities */
        env->fpscr |= 1 << FPSCR_VXISI;
        goto update_arith;
    case POWERPC_EXCP_FP_VXIDI:
        /* Division of infinity by infinity */
        env->fpscr |= 1 << FPSCR_VXIDI;
        goto update_arith;
    case POWERPC_EXCP_FP_VXZDZ:
        /* Division of zero by zero */
        env->fpscr |= 1 << FPSCR_VXZDZ;
        goto update_arith;
    case POWERPC_EXCP_FP_VXIMZ:
        /* Multiplication of zero by infinity */
        env->fpscr |= 1 << FPSCR_VXIMZ;
        goto update_arith;
    case POWERPC_EXCP_FP_VXVC:
        /* Ordered comparison of NaN */
        env->fpscr |= 1 << FPSCR_VXVC;
        if (set_fpcc) {
            env->fpscr &= ~(0xF << FPSCR_FPCC);
            env->fpscr |= 0x11 << FPSCR_FPCC;
        }
        /* We must update the target FPR before raising the exception */
        if (ve != 0) {
            cs->exception_index = POWERPC_EXCP_PROGRAM;
            env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_VXVC;
            /* Update the floating-point enabled exception summary */
            env->fpscr |= 1 << FPSCR_FEX;
            /* Exception is differed */
            ve = 0;
        }
        break;
    case POWERPC_EXCP_FP_VXSQRT:
        /* Square root of a negative number */
        env->fpscr |= 1 << FPSCR_VXSQRT;
    update_arith:
        env->fpscr &= ~((1 << FPSCR_FR) | (1 << FPSCR_FI));
        if (ve == 0) {
            /* Set the result to quiet NaN */
            ret = 0x7FF8000000000000ULL;
            if (set_fpcc) {
                env->fpscr &= ~(0xF << FPSCR_FPCC);
                env->fpscr |= 0x11 << FPSCR_FPCC;
            }
        }
        break;
    case POWERPC_EXCP_FP_VXCVI:
        /* Invalid conversion */
        env->fpscr |= 1 << FPSCR_VXCVI;
        env->fpscr &= ~((1 << FPSCR_FR) | (1 << FPSCR_FI));
        if (ve == 0) {
            /* Set the result to quiet NaN */
            ret = 0x7FF8000000000000ULL;
            if (set_fpcc) {
                env->fpscr &= ~(0xF << FPSCR_FPCC);
                env->fpscr |= 0x11 << FPSCR_FPCC;
            }
        }
        break;
    }
    /* Update the floating-point invalid operation summary */
    env->fpscr |= 1 << FPSCR_VX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (ve != 0) {
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        if (msr_fe0 != 0 || msr_fe1 != 0) {
            helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                       POWERPC_EXCP_FP | op);
        }
    }
    return ret;
}

static inline void float_zero_divide_excp(CPUPPCState *env)
{
    env->fpscr |= 1 << FPSCR_ZX;
    env->fpscr &= ~((1 << FPSCR_FR) | (1 << FPSCR_FI));
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_ze != 0) {
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        if (msr_fe0 != 0 || msr_fe1 != 0) {
            helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                       POWERPC_EXCP_FP | POWERPC_EXCP_FP_ZX);
        }
    }
}

static inline void float_overflow_excp(CPUPPCState *env)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));

    env->fpscr |= 1 << FPSCR_OX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_oe != 0) {
        /* XXX: should adjust the result */
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        cs->exception_index = POWERPC_EXCP_PROGRAM;
        env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_OX;
    } else {
        env->fpscr |= 1 << FPSCR_XX;
        env->fpscr |= 1 << FPSCR_FI;
    }
}

static inline void float_underflow_excp(CPUPPCState *env)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));

    env->fpscr |= 1 << FPSCR_UX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_ue != 0) {
        /* XXX: should adjust the result */
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        cs->exception_index = POWERPC_EXCP_PROGRAM;
        env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_UX;
    }
}

static inline void float_inexact_excp(CPUPPCState *env)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));

    env->fpscr |= 1 << FPSCR_XX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_xe != 0) {
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        cs->exception_index = POWERPC_EXCP_PROGRAM;
        env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_XX;
    }
}

static inline void fpscr_set_rounding_mode(CPUPPCState *env)
{
    int rnd_type;

    /* Set rounding mode */
    switch (fpscr_rn) {
    case 0:
        /* Best approximation (round to nearest) */
        rnd_type = float_round_nearest_even;
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        rnd_type = float_round_to_zero;
        break;
    case 2:
        /* Round toward +infinite */
        rnd_type = float_round_up;
        break;
    default:
    case 3:
        /* Round toward -infinite */
        rnd_type = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
}

void helper_fpscr_clrbit(CPUPPCState *env, uint32_t bit)
{
    int prev;

    prev = (env->fpscr >> bit) & 1;
    env->fpscr &= ~(1 << bit);
    if (prev == 1) {
        switch (bit) {
        case FPSCR_RN1:
        case FPSCR_RN:
            fpscr_set_rounding_mode(env);
            break;
        default:
            break;
        }
    }
}

void helper_fpscr_setbit(CPUPPCState *env, uint32_t bit)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    int prev;

    prev = (env->fpscr >> bit) & 1;
    env->fpscr |= 1 << bit;
    if (prev == 0) {
        switch (bit) {
        case FPSCR_VX:
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_ve) {
                goto raise_ve;
            }
            break;
        case FPSCR_OX:
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_oe) {
                goto raise_oe;
            }
            break;
        case FPSCR_UX:
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_ue) {
                goto raise_ue;
            }
            break;
        case FPSCR_ZX:
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_ze) {
                goto raise_ze;
            }
            break;
        case FPSCR_XX:
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_xe) {
                goto raise_xe;
            }
            break;
        case FPSCR_VXSNAN:
        case FPSCR_VXISI:
        case FPSCR_VXIDI:
        case FPSCR_VXZDZ:
        case FPSCR_VXIMZ:
        case FPSCR_VXVC:
        case FPSCR_VXSOFT:
        case FPSCR_VXSQRT:
        case FPSCR_VXCVI:
            env->fpscr |= 1 << FPSCR_VX;
            env->fpscr |= 1 << FPSCR_FX;
            if (fpscr_ve != 0) {
                goto raise_ve;
            }
            break;
        case FPSCR_VE:
            if (fpscr_vx != 0) {
            raise_ve:
                env->error_code = POWERPC_EXCP_FP;
                if (fpscr_vxsnan) {
                    env->error_code |= POWERPC_EXCP_FP_VXSNAN;
                }
                if (fpscr_vxisi) {
                    env->error_code |= POWERPC_EXCP_FP_VXISI;
                }
                if (fpscr_vxidi) {
                    env->error_code |= POWERPC_EXCP_FP_VXIDI;
                }
                if (fpscr_vxzdz) {
                    env->error_code |= POWERPC_EXCP_FP_VXZDZ;
                }
                if (fpscr_vximz) {
                    env->error_code |= POWERPC_EXCP_FP_VXIMZ;
                }
                if (fpscr_vxvc) {
                    env->error_code |= POWERPC_EXCP_FP_VXVC;
                }
                if (fpscr_vxsoft) {
                    env->error_code |= POWERPC_EXCP_FP_VXSOFT;
                }
                if (fpscr_vxsqrt) {
                    env->error_code |= POWERPC_EXCP_FP_VXSQRT;
                }
                if (fpscr_vxcvi) {
                    env->error_code |= POWERPC_EXCP_FP_VXCVI;
                }
                goto raise_excp;
            }
            break;
        case FPSCR_OE:
            if (fpscr_ox != 0) {
            raise_oe:
                env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_OX;
                goto raise_excp;
            }
            break;
        case FPSCR_UE:
            if (fpscr_ux != 0) {
            raise_ue:
                env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_UX;
                goto raise_excp;
            }
            break;
        case FPSCR_ZE:
            if (fpscr_zx != 0) {
            raise_ze:
                env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_ZX;
                goto raise_excp;
            }
            break;
        case FPSCR_XE:
            if (fpscr_xx != 0) {
            raise_xe:
                env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_XX;
                goto raise_excp;
            }
            break;
        case FPSCR_RN1:
        case FPSCR_RN:
            fpscr_set_rounding_mode(env);
            break;
        default:
            break;
        raise_excp:
            /* Update the floating-point enabled exception summary */
            env->fpscr |= 1 << FPSCR_FEX;
            /* We have to update Rc1 before raising the exception */
            cs->exception_index = POWERPC_EXCP_PROGRAM;
            break;
        }
    }
}

void helper_store_fpscr(CPUPPCState *env, uint64_t arg, uint32_t mask)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    target_ulong prev, new;
    int i;

    prev = env->fpscr;
    new = (target_ulong)arg;
    new &= ~0x60000000LL;
    new |= prev & 0x60000000LL;
    for (i = 0; i < sizeof(target_ulong) * 2; i++) {
        if (mask & (1 << i)) {
            env->fpscr &= ~(0xFLL << (4 * i));
            env->fpscr |= new & (0xFLL << (4 * i));
        }
    }
    /* Update VX and FEX */
    if (fpscr_ix != 0) {
        env->fpscr |= 1 << FPSCR_VX;
    } else {
        env->fpscr &= ~(1 << FPSCR_VX);
    }
    if ((fpscr_ex & fpscr_eex) != 0) {
        env->fpscr |= 1 << FPSCR_FEX;
        cs->exception_index = POWERPC_EXCP_PROGRAM;
        /* XXX: we should compute it properly */
        env->error_code = POWERPC_EXCP_FP;
    } else {
        env->fpscr &= ~(1 << FPSCR_FEX);
    }
    fpscr_set_rounding_mode(env);
}

void store_fpscr(CPUPPCState *env, uint64_t arg, uint32_t mask)
{
    helper_store_fpscr(env, arg, mask);
}

void helper_float_check_status(CPUPPCState *env)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    int status = get_float_exception_flags(&env->fp_status);

    if (status & float_flag_divbyzero) {
        float_zero_divide_excp(env);
    } else if (status & float_flag_overflow) {
        float_overflow_excp(env);
    } else if (status & float_flag_underflow) {
        float_underflow_excp(env);
    } else if (status & float_flag_inexact) {
        float_inexact_excp(env);
    }

    if (cs->exception_index == POWERPC_EXCP_PROGRAM &&
        (env->error_code & POWERPC_EXCP_FP)) {
        /* Differred floating-point exception after target FPR update */
        if (msr_fe0 != 0 || msr_fe1 != 0) {
            helper_raise_exception_err(env, cs->exception_index,
                                       env->error_code);
        }
    }
}

void helper_reset_fpstatus(CPUPPCState *env)
{
    set_float_exception_flags(0, &env->fp_status);
}

/* fadd - fadd. */
uint64_t helper_fadd(CPUPPCState *env, uint64_t arg1, uint64_t arg2)
{
    CPU_DoubleU farg1, farg2;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely(float64_is_infinity(farg1.d) && float64_is_infinity(farg2.d) &&
                 float64_is_neg(farg1.d) != float64_is_neg(farg2.d))) {
        /* Magnitude subtraction of infinities */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN addition */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg1.d = float64_add(farg1.d, farg2.d, &env->fp_status);
    }

    return farg1.ll;
}

/* fsub - fsub. */
uint64_t helper_fsub(CPUPPCState *env, uint64_t arg1, uint64_t arg2)
{
    CPU_DoubleU farg1, farg2;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely(float64_is_infinity(farg1.d) && float64_is_infinity(farg2.d) &&
                 float64_is_neg(farg1.d) == float64_is_neg(farg2.d))) {
        /* Magnitude subtraction of infinities */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN subtraction */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg1.d = float64_sub(farg1.d, farg2.d, &env->fp_status);
    }

    return farg1.ll;
}

/* fmul - fmul. */
uint64_t helper_fmul(CPUPPCState *env, uint64_t arg1, uint64_t arg2)
{
    CPU_DoubleU farg1, farg2;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely((float64_is_infinity(farg1.d) && float64_is_zero(farg2.d)) ||
                 (float64_is_zero(farg1.d) && float64_is_infinity(farg2.d)))) {
        /* Multiplication of zero by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN multiplication */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg1.d = float64_mul(farg1.d, farg2.d, &env->fp_status);
    }

    return farg1.ll;
}

/* fdiv - fdiv. */
uint64_t helper_fdiv(CPUPPCState *env, uint64_t arg1, uint64_t arg2)
{
    CPU_DoubleU farg1, farg2;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely(float64_is_infinity(farg1.d) &&
                 float64_is_infinity(farg2.d))) {
        /* Division of infinity by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIDI, 1);
    } else if (unlikely(float64_is_zero(farg1.d) && float64_is_zero(farg2.d))) {
        /* Division of zero by zero */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXZDZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN division */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg1.d = float64_div(farg1.d, farg2.d, &env->fp_status);
    }

    return farg1.ll;
}


#define FPU_FCTI(op, cvt, nanval)                                      \
uint64_t helper_##op(CPUPPCState *env, uint64_t arg)                   \
{                                                                      \
    CPU_DoubleU farg;                                                  \
                                                                       \
    farg.ll = arg;                                                     \
    farg.ll = float64_to_##cvt(farg.d, &env->fp_status);               \
                                                                       \
    if (unlikely(env->fp_status.float_exception_flags)) {              \
        if (float64_is_any_nan(arg)) {                                 \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI, 1);      \
            if (float64_is_signaling_nan(arg)) {                       \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1); \
            }                                                          \
            farg.ll = nanval;                                          \
        } else if (env->fp_status.float_exception_flags &              \
                   float_flag_invalid) {                               \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI, 1);      \
        }                                                              \
        helper_float_check_status(env);                                \
    }                                                                  \
    return farg.ll;                                                    \
 }

FPU_FCTI(fctiw, int32, 0x80000000U)
FPU_FCTI(fctiwz, int32_round_to_zero, 0x80000000U)
FPU_FCTI(fctiwu, uint32, 0x00000000U)
FPU_FCTI(fctiwuz, uint32_round_to_zero, 0x00000000U)
#if defined(TARGET_PPC64)
FPU_FCTI(fctid, int64, 0x8000000000000000ULL)
FPU_FCTI(fctidz, int64_round_to_zero, 0x8000000000000000ULL)
FPU_FCTI(fctidu, uint64, 0x0000000000000000ULL)
FPU_FCTI(fctiduz, uint64_round_to_zero, 0x0000000000000000ULL)
#endif

#if defined(TARGET_PPC64)

#define FPU_FCFI(op, cvtr, is_single)                      \
uint64_t helper_##op(CPUPPCState *env, uint64_t arg)       \
{                                                          \
    CPU_DoubleU farg;                                      \
                                                           \
    if (is_single) {                                       \
        float32 tmp = cvtr(arg, &env->fp_status);          \
        farg.d = float32_to_float64(tmp, &env->fp_status); \
    } else {                                               \
        farg.d = cvtr(arg, &env->fp_status);               \
    }                                                      \
    helper_float_check_status(env);                        \
    return farg.ll;                                        \
}

FPU_FCFI(fcfid, int64_to_float64, 0)
FPU_FCFI(fcfids, int64_to_float32, 1)
FPU_FCFI(fcfidu, uint64_to_float64, 0)
FPU_FCFI(fcfidus, uint64_to_float32, 1)

#endif

static inline uint64_t do_fri(CPUPPCState *env, uint64_t arg,
                              int rounding_mode)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN round */
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        farg.ll = arg | 0x0008000000000000ULL;
    } else {
        int inexact = get_float_exception_flags(&env->fp_status) &
                      float_flag_inexact;
        set_float_rounding_mode(rounding_mode, &env->fp_status);
        farg.ll = float64_round_to_int(farg.d, &env->fp_status);
        /* Restore rounding mode from FPSCR */
        fpscr_set_rounding_mode(env);

        /* fri* does not set FPSCR[XX] */
        if (!inexact) {
            env->fp_status.float_exception_flags &= ~float_flag_inexact;
        }
    }
    helper_float_check_status(env);
    return farg.ll;
}

uint64_t helper_frin(CPUPPCState *env, uint64_t arg)
{
    return do_fri(env, arg, float_round_ties_away);
}

uint64_t helper_friz(CPUPPCState *env, uint64_t arg)
{
    return do_fri(env, arg, float_round_to_zero);
}

uint64_t helper_frip(CPUPPCState *env, uint64_t arg)
{
    return do_fri(env, arg, float_round_up);
}

uint64_t helper_frim(CPUPPCState *env, uint64_t arg)
{
    return do_fri(env, arg, float_round_down);
}

/* fmadd - fmadd. */
uint64_t helper_fmadd(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                      uint64_t arg3)
{
    CPU_DoubleU farg1, farg2, farg3;

    farg1.ll = arg1;
    farg2.ll = arg2;
    farg3.ll = arg3;

    if (unlikely((float64_is_infinity(farg1.d) && float64_is_zero(farg2.d)) ||
                 (float64_is_zero(farg1.d) && float64_is_infinity(farg2.d)))) {
        /* Multiplication of zero by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        /* This is the way the PowerPC specification defines it */
        float128 ft0_128, ft1_128;

        ft0_128 = float64_to_float128(farg1.d, &env->fp_status);
        ft1_128 = float64_to_float128(farg2.d, &env->fp_status);
        ft0_128 = float128_mul(ft0_128, ft1_128, &env->fp_status);
        if (unlikely(float128_is_infinity(ft0_128) &&
                     float64_is_infinity(farg3.d) &&
                     float128_is_neg(ft0_128) != float64_is_neg(farg3.d))) {
            /* Magnitude subtraction of infinities */
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
        } else {
            ft1_128 = float64_to_float128(farg3.d, &env->fp_status);
            ft0_128 = float128_add(ft0_128, ft1_128, &env->fp_status);
            farg1.d = float128_to_float64(ft0_128, &env->fp_status);
        }
    }

    return farg1.ll;
}

/* fmsub - fmsub. */
uint64_t helper_fmsub(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                      uint64_t arg3)
{
    CPU_DoubleU farg1, farg2, farg3;

    farg1.ll = arg1;
    farg2.ll = arg2;
    farg3.ll = arg3;

    if (unlikely((float64_is_infinity(farg1.d) && float64_is_zero(farg2.d)) ||
                 (float64_is_zero(farg1.d) &&
                  float64_is_infinity(farg2.d)))) {
        /* Multiplication of zero by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        /* This is the way the PowerPC specification defines it */
        float128 ft0_128, ft1_128;

        ft0_128 = float64_to_float128(farg1.d, &env->fp_status);
        ft1_128 = float64_to_float128(farg2.d, &env->fp_status);
        ft0_128 = float128_mul(ft0_128, ft1_128, &env->fp_status);
        if (unlikely(float128_is_infinity(ft0_128) &&
                     float64_is_infinity(farg3.d) &&
                     float128_is_neg(ft0_128) == float64_is_neg(farg3.d))) {
            /* Magnitude subtraction of infinities */
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
        } else {
            ft1_128 = float64_to_float128(farg3.d, &env->fp_status);
            ft0_128 = float128_sub(ft0_128, ft1_128, &env->fp_status);
            farg1.d = float128_to_float64(ft0_128, &env->fp_status);
        }
    }
    return farg1.ll;
}

/* fnmadd - fnmadd. */
uint64_t helper_fnmadd(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                       uint64_t arg3)
{
    CPU_DoubleU farg1, farg2, farg3;

    farg1.ll = arg1;
    farg2.ll = arg2;
    farg3.ll = arg3;

    if (unlikely((float64_is_infinity(farg1.d) && float64_is_zero(farg2.d)) ||
                 (float64_is_zero(farg1.d) && float64_is_infinity(farg2.d)))) {
        /* Multiplication of zero by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        /* This is the way the PowerPC specification defines it */
        float128 ft0_128, ft1_128;

        ft0_128 = float64_to_float128(farg1.d, &env->fp_status);
        ft1_128 = float64_to_float128(farg2.d, &env->fp_status);
        ft0_128 = float128_mul(ft0_128, ft1_128, &env->fp_status);
        if (unlikely(float128_is_infinity(ft0_128) &&
                     float64_is_infinity(farg3.d) &&
                     float128_is_neg(ft0_128) != float64_is_neg(farg3.d))) {
            /* Magnitude subtraction of infinities */
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
        } else {
            ft1_128 = float64_to_float128(farg3.d, &env->fp_status);
            ft0_128 = float128_add(ft0_128, ft1_128, &env->fp_status);
            farg1.d = float128_to_float64(ft0_128, &env->fp_status);
        }
        if (likely(!float64_is_any_nan(farg1.d))) {
            farg1.d = float64_chs(farg1.d);
        }
    }
    return farg1.ll;
}

/* fnmsub - fnmsub. */
uint64_t helper_fnmsub(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                       uint64_t arg3)
{
    CPU_DoubleU farg1, farg2, farg3;

    farg1.ll = arg1;
    farg2.ll = arg2;
    farg3.ll = arg3;

    if (unlikely((float64_is_infinity(farg1.d) && float64_is_zero(farg2.d)) ||
                 (float64_is_zero(farg1.d) &&
                  float64_is_infinity(farg2.d)))) {
        /* Multiplication of zero by infinity */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        /* This is the way the PowerPC specification defines it */
        float128 ft0_128, ft1_128;

        ft0_128 = float64_to_float128(farg1.d, &env->fp_status);
        ft1_128 = float64_to_float128(farg2.d, &env->fp_status);
        ft0_128 = float128_mul(ft0_128, ft1_128, &env->fp_status);
        if (unlikely(float128_is_infinity(ft0_128) &&
                     float64_is_infinity(farg3.d) &&
                     float128_is_neg(ft0_128) == float64_is_neg(farg3.d))) {
            /* Magnitude subtraction of infinities */
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, 1);
        } else {
            ft1_128 = float64_to_float128(farg3.d, &env->fp_status);
            ft0_128 = float128_sub(ft0_128, ft1_128, &env->fp_status);
            farg1.d = float128_to_float64(ft0_128, &env->fp_status);
        }
        if (likely(!float64_is_any_nan(farg1.d))) {
            farg1.d = float64_chs(farg1.d);
        }
    }
    return farg1.ll;
}

/* frsp - frsp. */
uint64_t helper_frsp(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;
    float32 f32;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN square root */
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
    }
    f32 = float64_to_float32(farg.d, &env->fp_status);
    farg.d = float32_to_float64(f32, &env->fp_status);

    return farg.ll;
}

/* fsqrt - fsqrt. */
uint64_t helper_fsqrt(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_neg(farg.d) && !float64_is_zero(farg.d))) {
        /* Square root of a negative nonzero number */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg.d))) {
            /* sNaN square root */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg.d = float64_sqrt(farg.d, &env->fp_status);
    }
    return farg.ll;
}

/* fre - fre. */
uint64_t helper_fre(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN reciprocal */
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
    }
    farg.d = float64_div(float64_one, farg.d, &env->fp_status);
    return farg.d;
}

/* fres - fres. */
uint64_t helper_fres(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;
    float32 f32;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN reciprocal */
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
    }
    farg.d = float64_div(float64_one, farg.d, &env->fp_status);
    f32 = float64_to_float32(farg.d, &env->fp_status);
    farg.d = float32_to_float64(f32, &env->fp_status);

    return farg.ll;
}

/* frsqrte  - frsqrte. */
uint64_t helper_frsqrte(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;
    float32 f32;

    farg.ll = arg;

    if (unlikely(float64_is_neg(farg.d) && !float64_is_zero(farg.d))) {
        /* Reciprocal square root of a negative nonzero number */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT, 1);
    } else {
        if (unlikely(float64_is_signaling_nan(farg.d))) {
            /* sNaN reciprocal square root */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
        }
        farg.d = float64_sqrt(farg.d, &env->fp_status);
        farg.d = float64_div(float64_one, farg.d, &env->fp_status);
        f32 = float64_to_float32(farg.d, &env->fp_status);
        farg.d = float32_to_float64(f32, &env->fp_status);
    }
    return farg.ll;
}

/* fsel - fsel. */
uint64_t helper_fsel(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3)
{
    CPU_DoubleU farg1;

    farg1.ll = arg1;

    if ((!float64_is_neg(farg1.d) || float64_is_zero(farg1.d)) &&
        !float64_is_any_nan(farg1.d)) {
        return arg2;
    } else {
        return arg3;
    }
}

uint32_t helper_ftdiv(uint64_t fra, uint64_t frb)
{
    int fe_flag = 0;
    int fg_flag = 0;

    if (unlikely(float64_is_infinity(fra) ||
                 float64_is_infinity(frb) ||
                 float64_is_zero(frb))) {
        fe_flag = 1;
        fg_flag = 1;
    } else {
        int e_a = ppc_float64_get_unbiased_exp(fra);
        int e_b = ppc_float64_get_unbiased_exp(frb);

        if (unlikely(float64_is_any_nan(fra) ||
                     float64_is_any_nan(frb))) {
            fe_flag = 1;
        } else if ((e_b <= -1022) || (e_b >= 1021)) {
            fe_flag = 1;
        } else if (!float64_is_zero(fra) &&
                   (((e_a - e_b) >= 1023) ||
                    ((e_a - e_b) <= -1021) ||
                    (e_a <= -970))) {
            fe_flag = 1;
        }

        if (unlikely(float64_is_zero_or_denormal(frb))) {
            /* XB is not zero because of the above check and */
            /* so must be denormalized.                      */
            fg_flag = 1;
        }
    }

    return 0x8 | (fg_flag ? 4 : 0) | (fe_flag ? 2 : 0);
}

uint32_t helper_ftsqrt(uint64_t frb)
{
    int fe_flag = 0;
    int fg_flag = 0;

    if (unlikely(float64_is_infinity(frb) || float64_is_zero(frb))) {
        fe_flag = 1;
        fg_flag = 1;
    } else {
        int e_b = ppc_float64_get_unbiased_exp(frb);

        if (unlikely(float64_is_any_nan(frb))) {
            fe_flag = 1;
        } else if (unlikely(float64_is_zero(frb))) {
            fe_flag = 1;
        } else if (unlikely(float64_is_neg(frb))) {
            fe_flag = 1;
        } else if (!float64_is_zero(frb) && (e_b <= (-1022+52))) {
            fe_flag = 1;
        }

        if (unlikely(float64_is_zero_or_denormal(frb))) {
            /* XB is not zero because of the above check and */
            /* therefore must be denormalized.               */
            fg_flag = 1;
        }
    }

    return 0x8 | (fg_flag ? 4 : 0) | (fe_flag ? 2 : 0);
}

void helper_fcmpu(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                  uint32_t crfD)
{
    CPU_DoubleU farg1, farg2;
    uint32_t ret = 0;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely(float64_is_any_nan(farg1.d) ||
                 float64_is_any_nan(farg2.d))) {
        ret = 0x01UL;
    } else if (float64_lt(farg1.d, farg2.d, &env->fp_status)) {
        ret = 0x08UL;
    } else if (!float64_le(farg1.d, farg2.d, &env->fp_status)) {
        ret = 0x04UL;
    } else {
        ret = 0x02UL;
    }

    env->fpscr &= ~(0x0F << FPSCR_FPRF);
    env->fpscr |= ret << FPSCR_FPRF;
    env->crf[crfD] = ret;
    if (unlikely(ret == 0x01UL
                 && (float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d)))) {
        /* sNaN comparison */
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 1);
    }
}

void helper_fcmpo(CPUPPCState *env, uint64_t arg1, uint64_t arg2,
                  uint32_t crfD)
{
    CPU_DoubleU farg1, farg2;
    uint32_t ret = 0;

    farg1.ll = arg1;
    farg2.ll = arg2;

    if (unlikely(float64_is_any_nan(farg1.d) ||
                 float64_is_any_nan(farg2.d))) {
        ret = 0x01UL;
    } else if (float64_lt(farg1.d, farg2.d, &env->fp_status)) {
        ret = 0x08UL;
    } else if (!float64_le(farg1.d, farg2.d, &env->fp_status)) {
        ret = 0x04UL;
    } else {
        ret = 0x02UL;
    }

    env->fpscr &= ~(0x0F << FPSCR_FPRF);
    env->fpscr |= ret << FPSCR_FPRF;
    env->crf[crfD] = ret;
    if (unlikely(ret == 0x01UL)) {
        if (float64_is_signaling_nan(farg1.d) ||
            float64_is_signaling_nan(farg2.d)) {
            /* sNaN comparison */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                  POWERPC_EXCP_FP_VXVC, 1);
        } else {
            /* qNaN comparison */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXVC, 1);
        }
    }
}

/* Single-precision floating-point conversions */
static inline uint32_t efscfsi(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.f = int32_to_float32(val, &env->vec_status);

    return u.l;
}

static inline uint32_t efscfui(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.f = uint32_to_float32(val, &env->vec_status);

    return u.l;
}

static inline int32_t efsctsi(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }

    return float32_to_int32(u.f, &env->vec_status);
}

static inline uint32_t efsctui(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }

    return float32_to_uint32(u.f, &env->vec_status);
}

static inline uint32_t efsctsiz(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }

    return float32_to_int32_round_to_zero(u.f, &env->vec_status);
}

static inline uint32_t efsctuiz(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }

    return float32_to_uint32_round_to_zero(u.f, &env->vec_status);
}

static inline uint32_t efscfsf(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;
    float32 tmp;

    u.f = int32_to_float32(val, &env->vec_status);
    tmp = int64_to_float32(1ULL << 32, &env->vec_status);
    u.f = float32_div(u.f, tmp, &env->vec_status);

    return u.l;
}

static inline uint32_t efscfuf(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;
    float32 tmp;

    u.f = uint32_to_float32(val, &env->vec_status);
    tmp = uint64_to_float32(1ULL << 32, &env->vec_status);
    u.f = float32_div(u.f, tmp, &env->vec_status);

    return u.l;
}

static inline uint32_t efsctsf(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;
    float32 tmp;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }
    tmp = uint64_to_float32(1ULL << 32, &env->vec_status);
    u.f = float32_mul(u.f, tmp, &env->vec_status);

    return float32_to_int32(u.f, &env->vec_status);
}

static inline uint32_t efsctuf(CPUPPCState *env, uint32_t val)
{
    CPU_FloatU u;
    float32 tmp;

    u.l = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float32_is_quiet_nan(u.f))) {
        return 0;
    }
    tmp = uint64_to_float32(1ULL << 32, &env->vec_status);
    u.f = float32_mul(u.f, tmp, &env->vec_status);

    return float32_to_uint32(u.f, &env->vec_status);
}

#define HELPER_SPE_SINGLE_CONV(name)                              \
    uint32_t helper_e##name(CPUPPCState *env, uint32_t val)       \
    {                                                             \
        return e##name(env, val);                                 \
    }
/* efscfsi */
HELPER_SPE_SINGLE_CONV(fscfsi);
/* efscfui */
HELPER_SPE_SINGLE_CONV(fscfui);
/* efscfuf */
HELPER_SPE_SINGLE_CONV(fscfuf);
/* efscfsf */
HELPER_SPE_SINGLE_CONV(fscfsf);
/* efsctsi */
HELPER_SPE_SINGLE_CONV(fsctsi);
/* efsctui */
HELPER_SPE_SINGLE_CONV(fsctui);
/* efsctsiz */
HELPER_SPE_SINGLE_CONV(fsctsiz);
/* efsctuiz */
HELPER_SPE_SINGLE_CONV(fsctuiz);
/* efsctsf */
HELPER_SPE_SINGLE_CONV(fsctsf);
/* efsctuf */
HELPER_SPE_SINGLE_CONV(fsctuf);

#define HELPER_SPE_VECTOR_CONV(name)                            \
    uint64_t helper_ev##name(CPUPPCState *env, uint64_t val)    \
    {                                                           \
        return ((uint64_t)e##name(env, val >> 32) << 32) |      \
            (uint64_t)e##name(env, val);                        \
    }
/* evfscfsi */
HELPER_SPE_VECTOR_CONV(fscfsi);
/* evfscfui */
HELPER_SPE_VECTOR_CONV(fscfui);
/* evfscfuf */
HELPER_SPE_VECTOR_CONV(fscfuf);
/* evfscfsf */
HELPER_SPE_VECTOR_CONV(fscfsf);
/* evfsctsi */
HELPER_SPE_VECTOR_CONV(fsctsi);
/* evfsctui */
HELPER_SPE_VECTOR_CONV(fsctui);
/* evfsctsiz */
HELPER_SPE_VECTOR_CONV(fsctsiz);
/* evfsctuiz */
HELPER_SPE_VECTOR_CONV(fsctuiz);
/* evfsctsf */
HELPER_SPE_VECTOR_CONV(fsctsf);
/* evfsctuf */
HELPER_SPE_VECTOR_CONV(fsctuf);

/* Single-precision floating-point arithmetic */
static inline uint32_t efsadd(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    u1.f = float32_add(u1.f, u2.f, &env->vec_status);
    return u1.l;
}

static inline uint32_t efssub(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    u1.f = float32_sub(u1.f, u2.f, &env->vec_status);
    return u1.l;
}

static inline uint32_t efsmul(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    u1.f = float32_mul(u1.f, u2.f, &env->vec_status);
    return u1.l;
}

static inline uint32_t efsdiv(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    u1.f = float32_div(u1.f, u2.f, &env->vec_status);
    return u1.l;
}

#define HELPER_SPE_SINGLE_ARITH(name)                                   \
    uint32_t helper_e##name(CPUPPCState *env, uint32_t op1, uint32_t op2) \
    {                                                                   \
        return e##name(env, op1, op2);                                  \
    }
/* efsadd */
HELPER_SPE_SINGLE_ARITH(fsadd);
/* efssub */
HELPER_SPE_SINGLE_ARITH(fssub);
/* efsmul */
HELPER_SPE_SINGLE_ARITH(fsmul);
/* efsdiv */
HELPER_SPE_SINGLE_ARITH(fsdiv);

#define HELPER_SPE_VECTOR_ARITH(name)                                   \
    uint64_t helper_ev##name(CPUPPCState *env, uint64_t op1, uint64_t op2) \
    {                                                                   \
        return ((uint64_t)e##name(env, op1 >> 32, op2 >> 32) << 32) |   \
            (uint64_t)e##name(env, op1, op2);                           \
    }
/* evfsadd */
HELPER_SPE_VECTOR_ARITH(fsadd);
/* evfssub */
HELPER_SPE_VECTOR_ARITH(fssub);
/* evfsmul */
HELPER_SPE_VECTOR_ARITH(fsmul);
/* evfsdiv */
HELPER_SPE_VECTOR_ARITH(fsdiv);

/* Single-precision floating-point comparisons */
static inline uint32_t efscmplt(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    return float32_lt(u1.f, u2.f, &env->vec_status) ? 4 : 0;
}

static inline uint32_t efscmpgt(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    return float32_le(u1.f, u2.f, &env->vec_status) ? 0 : 4;
}

static inline uint32_t efscmpeq(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    CPU_FloatU u1, u2;

    u1.l = op1;
    u2.l = op2;
    return float32_eq(u1.f, u2.f, &env->vec_status) ? 4 : 0;
}

static inline uint32_t efststlt(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    /* XXX: TODO: ignore special values (NaN, infinites, ...) */
    return efscmplt(env, op1, op2);
}

static inline uint32_t efststgt(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    /* XXX: TODO: ignore special values (NaN, infinites, ...) */
    return efscmpgt(env, op1, op2);
}

static inline uint32_t efststeq(CPUPPCState *env, uint32_t op1, uint32_t op2)
{
    /* XXX: TODO: ignore special values (NaN, infinites, ...) */
    return efscmpeq(env, op1, op2);
}

#define HELPER_SINGLE_SPE_CMP(name)                                     \
    uint32_t helper_e##name(CPUPPCState *env, uint32_t op1, uint32_t op2) \
    {                                                                   \
        return e##name(env, op1, op2) << 2;                             \
    }
/* efststlt */
HELPER_SINGLE_SPE_CMP(fststlt);
/* efststgt */
HELPER_SINGLE_SPE_CMP(fststgt);
/* efststeq */
HELPER_SINGLE_SPE_CMP(fststeq);
/* efscmplt */
HELPER_SINGLE_SPE_CMP(fscmplt);
/* efscmpgt */
HELPER_SINGLE_SPE_CMP(fscmpgt);
/* efscmpeq */
HELPER_SINGLE_SPE_CMP(fscmpeq);

static inline uint32_t evcmp_merge(int t0, int t1)
{
    return (t0 << 3) | (t1 << 2) | ((t0 | t1) << 1) | (t0 & t1);
}

#define HELPER_VECTOR_SPE_CMP(name)                                     \
    uint32_t helper_ev##name(CPUPPCState *env, uint64_t op1, uint64_t op2) \
    {                                                                   \
        return evcmp_merge(e##name(env, op1 >> 32, op2 >> 32),          \
                           e##name(env, op1, op2));                     \
    }
/* evfststlt */
HELPER_VECTOR_SPE_CMP(fststlt);
/* evfststgt */
HELPER_VECTOR_SPE_CMP(fststgt);
/* evfststeq */
HELPER_VECTOR_SPE_CMP(fststeq);
/* evfscmplt */
HELPER_VECTOR_SPE_CMP(fscmplt);
/* evfscmpgt */
HELPER_VECTOR_SPE_CMP(fscmpgt);
/* evfscmpeq */
HELPER_VECTOR_SPE_CMP(fscmpeq);

/* Double-precision floating-point conversion */
uint64_t helper_efdcfsi(CPUPPCState *env, uint32_t val)
{
    CPU_DoubleU u;

    u.d = int32_to_float64(val, &env->vec_status);

    return u.ll;
}

uint64_t helper_efdcfsid(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.d = int64_to_float64(val, &env->vec_status);

    return u.ll;
}

uint64_t helper_efdcfui(CPUPPCState *env, uint32_t val)
{
    CPU_DoubleU u;

    u.d = uint32_to_float64(val, &env->vec_status);

    return u.ll;
}

uint64_t helper_efdcfuid(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.d = uint64_to_float64(val, &env->vec_status);

    return u.ll;
}

uint32_t helper_efdctsi(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_int32(u.d, &env->vec_status);
}

uint32_t helper_efdctui(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_uint32(u.d, &env->vec_status);
}

uint32_t helper_efdctsiz(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_int32_round_to_zero(u.d, &env->vec_status);
}

uint64_t helper_efdctsidz(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_int64_round_to_zero(u.d, &env->vec_status);
}

uint32_t helper_efdctuiz(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_uint32_round_to_zero(u.d, &env->vec_status);
}

uint64_t helper_efdctuidz(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }

    return float64_to_uint64_round_to_zero(u.d, &env->vec_status);
}

uint64_t helper_efdcfsf(CPUPPCState *env, uint32_t val)
{
    CPU_DoubleU u;
    float64 tmp;

    u.d = int32_to_float64(val, &env->vec_status);
    tmp = int64_to_float64(1ULL << 32, &env->vec_status);
    u.d = float64_div(u.d, tmp, &env->vec_status);

    return u.ll;
}

uint64_t helper_efdcfuf(CPUPPCState *env, uint32_t val)
{
    CPU_DoubleU u;
    float64 tmp;

    u.d = uint32_to_float64(val, &env->vec_status);
    tmp = int64_to_float64(1ULL << 32, &env->vec_status);
    u.d = float64_div(u.d, tmp, &env->vec_status);

    return u.ll;
}

uint32_t helper_efdctsf(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;
    float64 tmp;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }
    tmp = uint64_to_float64(1ULL << 32, &env->vec_status);
    u.d = float64_mul(u.d, tmp, &env->vec_status);

    return float64_to_int32(u.d, &env->vec_status);
}

uint32_t helper_efdctuf(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u;
    float64 tmp;

    u.ll = val;
    /* NaN are not treated the same way IEEE 754 does */
    if (unlikely(float64_is_any_nan(u.d))) {
        return 0;
    }
    tmp = uint64_to_float64(1ULL << 32, &env->vec_status);
    u.d = float64_mul(u.d, tmp, &env->vec_status);

    return float64_to_uint32(u.d, &env->vec_status);
}

uint32_t helper_efscfd(CPUPPCState *env, uint64_t val)
{
    CPU_DoubleU u1;
    CPU_FloatU u2;

    u1.ll = val;
    u2.f = float64_to_float32(u1.d, &env->vec_status);

    return u2.l;
}

uint64_t helper_efdcfs(CPUPPCState *env, uint32_t val)
{
    CPU_DoubleU u2;
    CPU_FloatU u1;

    u1.l = val;
    u2.d = float32_to_float64(u1.f, &env->vec_status);

    return u2.ll;
}

/* Double precision fixed-point arithmetic */
uint64_t helper_efdadd(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    u1.d = float64_add(u1.d, u2.d, &env->vec_status);
    return u1.ll;
}

uint64_t helper_efdsub(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    u1.d = float64_sub(u1.d, u2.d, &env->vec_status);
    return u1.ll;
}

uint64_t helper_efdmul(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    u1.d = float64_mul(u1.d, u2.d, &env->vec_status);
    return u1.ll;
}

uint64_t helper_efddiv(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    u1.d = float64_div(u1.d, u2.d, &env->vec_status);
    return u1.ll;
}

/* Double precision floating point helpers */
uint32_t helper_efdtstlt(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    return float64_lt(u1.d, u2.d, &env->vec_status) ? 4 : 0;
}

uint32_t helper_efdtstgt(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    return float64_le(u1.d, u2.d, &env->vec_status) ? 0 : 4;
}

uint32_t helper_efdtsteq(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    CPU_DoubleU u1, u2;

    u1.ll = op1;
    u2.ll = op2;
    return float64_eq_quiet(u1.d, u2.d, &env->vec_status) ? 4 : 0;
}

uint32_t helper_efdcmplt(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    /* XXX: TODO: test special values (NaN, infinites, ...) */
    return helper_efdtstlt(env, op1, op2);
}

uint32_t helper_efdcmpgt(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    /* XXX: TODO: test special values (NaN, infinites, ...) */
    return helper_efdtstgt(env, op1, op2);
}

uint32_t helper_efdcmpeq(CPUPPCState *env, uint64_t op1, uint64_t op2)
{
    /* XXX: TODO: test special values (NaN, infinites, ...) */
    return helper_efdtsteq(env, op1, op2);
}

#define DECODE_SPLIT(opcode, shift1, nb1, shift2, nb2) \
    (((((opcode) >> (shift1)) & ((1 << (nb1)) - 1)) << nb2) |    \
     (((opcode) >> (shift2)) & ((1 << (nb2)) - 1)))

#define xT(opcode) DECODE_SPLIT(opcode, 0, 1, 21, 5)
#define xA(opcode) DECODE_SPLIT(opcode, 2, 1, 16, 5)
#define xB(opcode) DECODE_SPLIT(opcode, 1, 1, 11, 5)
#define xC(opcode) DECODE_SPLIT(opcode, 3, 1,  6, 5)
#define BF(opcode) (((opcode) >> (31-8)) & 7)

typedef union _ppc_vsr_t {
    uint64_t u64[2];
    uint32_t u32[4];
    float32 f32[4];
    float64 f64[2];
} ppc_vsr_t;

#if defined(HOST_WORDS_BIGENDIAN)
#define VsrW(i) u32[i]
#define VsrD(i) u64[i]
#else
#define VsrW(i) u32[3-(i)]
#define VsrD(i) u64[1-(i)]
#endif

static void getVSR(int n, ppc_vsr_t *vsr, CPUPPCState *env)
{
    if (n < 32) {
        vsr->VsrD(0) = env->fpr[n];
        vsr->VsrD(1) = env->vsr[n];
    } else {
        vsr->u64[0] = env->avr[n-32].u64[0];
        vsr->u64[1] = env->avr[n-32].u64[1];
    }
}

static void putVSR(int n, ppc_vsr_t *vsr, CPUPPCState *env)
{
    if (n < 32) {
        env->fpr[n] = vsr->VsrD(0);
        env->vsr[n] = vsr->VsrD(1);
    } else {
        env->avr[n-32].u64[0] = vsr->u64[0];
        env->avr[n-32].u64[1] = vsr->u64[1];
    }
}

#define float64_to_float64(x, env) x


/* VSX_ADD_SUB - VSX floating point add/subract
 *   name  - instruction mnemonic
 *   op    - operation (add or sub)
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_ADD_SUB(name, op, nels, tp, fld, sfprf, r2sp)                    \
void helper_##name(CPUPPCState *env, uint32_t opcode)                        \
{                                                                            \
    ppc_vsr_t xt, xa, xb;                                                    \
    int i;                                                                   \
                                                                             \
    getVSR(xA(opcode), &xa, env);                                            \
    getVSR(xB(opcode), &xb, env);                                            \
    getVSR(xT(opcode), &xt, env);                                            \
    helper_reset_fpstatus(env);                                              \
                                                                             \
    for (i = 0; i < nels; i++) {                                             \
        float_status tstat = env->fp_status;                                 \
        set_float_exception_flags(0, &tstat);                                \
        xt.fld = tp##_##op(xa.fld, xb.fld, &tstat);                          \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags; \
                                                                             \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {    \
            if (tp##_is_infinity(xa.fld) && tp##_is_infinity(xb.fld)) {      \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, sfprf);    \
            } else if (tp##_is_signaling_nan(xa.fld) ||                      \
                       tp##_is_signaling_nan(xb.fld)) {                      \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);   \
            }                                                                \
        }                                                                    \
                                                                             \
        if (r2sp) {                                                          \
            xt.fld = helper_frsp(env, xt.fld);                               \
        }                                                                    \
                                                                             \
        if (sfprf) {                                                         \
            helper_compute_fprf(env, xt.fld, sfprf);                         \
        }                                                                    \
    }                                                                        \
    putVSR(xT(opcode), &xt, env);                                            \
    helper_float_check_status(env);                                          \
}

VSX_ADD_SUB(xsadddp, add, 1, float64, VsrD(0), 1, 0)
VSX_ADD_SUB(xsaddsp, add, 1, float64, VsrD(0), 1, 1)
VSX_ADD_SUB(xvadddp, add, 2, float64, VsrD(i), 0, 0)
VSX_ADD_SUB(xvaddsp, add, 4, float32, VsrW(i), 0, 0)
VSX_ADD_SUB(xssubdp, sub, 1, float64, VsrD(0), 1, 0)
VSX_ADD_SUB(xssubsp, sub, 1, float64, VsrD(0), 1, 1)
VSX_ADD_SUB(xvsubdp, sub, 2, float64, VsrD(i), 0, 0)
VSX_ADD_SUB(xvsubsp, sub, 4, float32, VsrW(i), 0, 0)

/* VSX_MUL - VSX floating point multiply
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_MUL(op, nels, tp, fld, sfprf, r2sp)                              \
void helper_##op(CPUPPCState *env, uint32_t opcode)                          \
{                                                                            \
    ppc_vsr_t xt, xa, xb;                                                    \
    int i;                                                                   \
                                                                             \
    getVSR(xA(opcode), &xa, env);                                            \
    getVSR(xB(opcode), &xb, env);                                            \
    getVSR(xT(opcode), &xt, env);                                            \
    helper_reset_fpstatus(env);                                              \
                                                                             \
    for (i = 0; i < nels; i++) {                                             \
        float_status tstat = env->fp_status;                                 \
        set_float_exception_flags(0, &tstat);                                \
        xt.fld = tp##_mul(xa.fld, xb.fld, &tstat);                           \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags; \
                                                                             \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {    \
            if ((tp##_is_infinity(xa.fld) && tp##_is_zero(xb.fld)) ||        \
                (tp##_is_infinity(xb.fld) && tp##_is_zero(xa.fld))) {        \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ, sfprf);    \
            } else if (tp##_is_signaling_nan(xa.fld) ||                      \
                       tp##_is_signaling_nan(xb.fld)) {                      \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);   \
            }                                                                \
        }                                                                    \
                                                                             \
        if (r2sp) {                                                          \
            xt.fld = helper_frsp(env, xt.fld);                               \
        }                                                                    \
                                                                             \
        if (sfprf) {                                                         \
            helper_compute_fprf(env, xt.fld, sfprf);                         \
        }                                                                    \
    }                                                                        \
                                                                             \
    putVSR(xT(opcode), &xt, env);                                            \
    helper_float_check_status(env);                                          \
}

VSX_MUL(xsmuldp, 1, float64, VsrD(0), 1, 0)
VSX_MUL(xsmulsp, 1, float64, VsrD(0), 1, 1)
VSX_MUL(xvmuldp, 2, float64, VsrD(i), 0, 0)
VSX_MUL(xvmulsp, 4, float32, VsrW(i), 0, 0)

/* VSX_DIV - VSX floating point divide
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_DIV(op, nels, tp, fld, sfprf, r2sp)                               \
void helper_##op(CPUPPCState *env, uint32_t opcode)                           \
{                                                                             \
    ppc_vsr_t xt, xa, xb;                                                     \
    int i;                                                                    \
                                                                              \
    getVSR(xA(opcode), &xa, env);                                             \
    getVSR(xB(opcode), &xb, env);                                             \
    getVSR(xT(opcode), &xt, env);                                             \
    helper_reset_fpstatus(env);                                               \
                                                                              \
    for (i = 0; i < nels; i++) {                                              \
        float_status tstat = env->fp_status;                                  \
        set_float_exception_flags(0, &tstat);                                 \
        xt.fld = tp##_div(xa.fld, xb.fld, &tstat);                            \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags;  \
                                                                              \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {     \
            if (tp##_is_infinity(xa.fld) && tp##_is_infinity(xb.fld)) {       \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIDI, sfprf);     \
            } else if (tp##_is_zero(xa.fld) &&                                \
                tp##_is_zero(xb.fld)) {                                       \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXZDZ, sfprf);     \
            } else if (tp##_is_signaling_nan(xa.fld) ||                       \
                tp##_is_signaling_nan(xb.fld)) {                              \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);    \
            }                                                                 \
        }                                                                     \
                                                                              \
        if (r2sp) {                                                           \
            xt.fld = helper_frsp(env, xt.fld);                                \
        }                                                                     \
                                                                              \
        if (sfprf) {                                                          \
            helper_compute_fprf(env, xt.fld, sfprf);                          \
        }                                                                     \
    }                                                                         \
                                                                              \
    putVSR(xT(opcode), &xt, env);                                             \
    helper_float_check_status(env);                                           \
}

VSX_DIV(xsdivdp, 1, float64, VsrD(0), 1, 0)
VSX_DIV(xsdivsp, 1, float64, VsrD(0), 1, 1)
VSX_DIV(xvdivdp, 2, float64, VsrD(i), 0, 0)
VSX_DIV(xvdivsp, 4, float32, VsrW(i), 0, 0)

/* VSX_RE  - VSX floating point reciprocal estimate
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_RE(op, nels, tp, fld, sfprf, r2sp)                                \
void helper_##op(CPUPPCState *env, uint32_t opcode)                           \
{                                                                             \
    ppc_vsr_t xt, xb;                                                         \
    int i;                                                                    \
                                                                              \
    getVSR(xB(opcode), &xb, env);                                             \
    getVSR(xT(opcode), &xt, env);                                             \
    helper_reset_fpstatus(env);                                               \
                                                                              \
    for (i = 0; i < nels; i++) {                                              \
        if (unlikely(tp##_is_signaling_nan(xb.fld))) {                        \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);    \
        }                                                                     \
        xt.fld = tp##_div(tp##_one, xb.fld, &env->fp_status);                 \
                                                                              \
        if (r2sp) {                                                           \
            xt.fld = helper_frsp(env, xt.fld);                                \
        }                                                                     \
                                                                              \
        if (sfprf) {                                                          \
            helper_compute_fprf(env, xt.fld, sfprf);                          \
        }                                                                     \
    }                                                                         \
                                                                              \
    putVSR(xT(opcode), &xt, env);                                             \
    helper_float_check_status(env);                                           \
}

VSX_RE(xsredp, 1, float64, VsrD(0), 1, 0)
VSX_RE(xsresp, 1, float64, VsrD(0), 1, 1)
VSX_RE(xvredp, 2, float64, VsrD(i), 0, 0)
VSX_RE(xvresp, 4, float32, VsrW(i), 0, 0)

/* VSX_SQRT - VSX floating point square root
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_SQRT(op, nels, tp, fld, sfprf, r2sp)                             \
void helper_##op(CPUPPCState *env, uint32_t opcode)                          \
{                                                                            \
    ppc_vsr_t xt, xb;                                                        \
    int i;                                                                   \
                                                                             \
    getVSR(xB(opcode), &xb, env);                                            \
    getVSR(xT(opcode), &xt, env);                                            \
    helper_reset_fpstatus(env);                                              \
                                                                             \
    for (i = 0; i < nels; i++) {                                             \
        float_status tstat = env->fp_status;                                 \
        set_float_exception_flags(0, &tstat);                                \
        xt.fld = tp##_sqrt(xb.fld, &tstat);                                  \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags; \
                                                                             \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {    \
            if (tp##_is_neg(xb.fld) && !tp##_is_zero(xb.fld)) {              \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT, sfprf);   \
            } else if (tp##_is_signaling_nan(xb.fld)) {                      \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);   \
            }                                                                \
        }                                                                    \
                                                                             \
        if (r2sp) {                                                          \
            xt.fld = helper_frsp(env, xt.fld);                               \
        }                                                                    \
                                                                             \
        if (sfprf) {                                                         \
            helper_compute_fprf(env, xt.fld, sfprf);                         \
        }                                                                    \
    }                                                                        \
                                                                             \
    putVSR(xT(opcode), &xt, env);                                            \
    helper_float_check_status(env);                                          \
}

VSX_SQRT(xssqrtdp, 1, float64, VsrD(0), 1, 0)
VSX_SQRT(xssqrtsp, 1, float64, VsrD(0), 1, 1)
VSX_SQRT(xvsqrtdp, 2, float64, VsrD(i), 0, 0)
VSX_SQRT(xvsqrtsp, 4, float32, VsrW(i), 0, 0)

/* VSX_RSQRTE - VSX floating point reciprocal square root estimate
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   sfprf - set FPRF
 */
#define VSX_RSQRTE(op, nels, tp, fld, sfprf, r2sp)                           \
void helper_##op(CPUPPCState *env, uint32_t opcode)                          \
{                                                                            \
    ppc_vsr_t xt, xb;                                                        \
    int i;                                                                   \
                                                                             \
    getVSR(xB(opcode), &xb, env);                                            \
    getVSR(xT(opcode), &xt, env);                                            \
    helper_reset_fpstatus(env);                                              \
                                                                             \
    for (i = 0; i < nels; i++) {                                             \
        float_status tstat = env->fp_status;                                 \
        set_float_exception_flags(0, &tstat);                                \
        xt.fld = tp##_sqrt(xb.fld, &tstat);                                  \
        xt.fld = tp##_div(tp##_one, xt.fld, &tstat);                         \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags; \
                                                                             \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {    \
            if (tp##_is_neg(xb.fld) && !tp##_is_zero(xb.fld)) {              \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT, sfprf);   \
            } else if (tp##_is_signaling_nan(xb.fld)) {                      \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);   \
            }                                                                \
        }                                                                    \
                                                                             \
        if (r2sp) {                                                          \
            xt.fld = helper_frsp(env, xt.fld);                               \
        }                                                                    \
                                                                             \
        if (sfprf) {                                                         \
            helper_compute_fprf(env, xt.fld, sfprf);                         \
        }                                                                    \
    }                                                                        \
                                                                             \
    putVSR(xT(opcode), &xt, env);                                            \
    helper_float_check_status(env);                                          \
}

VSX_RSQRTE(xsrsqrtedp, 1, float64, VsrD(0), 1, 0)
VSX_RSQRTE(xsrsqrtesp, 1, float64, VsrD(0), 1, 1)
VSX_RSQRTE(xvrsqrtedp, 2, float64, VsrD(i), 0, 0)
VSX_RSQRTE(xvrsqrtesp, 4, float32, VsrW(i), 0, 0)

/* VSX_TDIV - VSX floating point test for divide
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   emin  - minimum unbiased exponent
 *   emax  - maximum unbiased exponent
 *   nbits - number of fraction bits
 */
#define VSX_TDIV(op, nels, tp, fld, emin, emax, nbits)                  \
void helper_##op(CPUPPCState *env, uint32_t opcode)                     \
{                                                                       \
    ppc_vsr_t xa, xb;                                                   \
    int i;                                                              \
    int fe_flag = 0;                                                    \
    int fg_flag = 0;                                                    \
                                                                        \
    getVSR(xA(opcode), &xa, env);                                       \
    getVSR(xB(opcode), &xb, env);                                       \
                                                                        \
    for (i = 0; i < nels; i++) {                                        \
        if (unlikely(tp##_is_infinity(xa.fld) ||                        \
                     tp##_is_infinity(xb.fld) ||                        \
                     tp##_is_zero(xb.fld))) {                           \
            fe_flag = 1;                                                \
            fg_flag = 1;                                                \
        } else {                                                        \
            int e_a = ppc_##tp##_get_unbiased_exp(xa.fld);              \
            int e_b = ppc_##tp##_get_unbiased_exp(xb.fld);              \
                                                                        \
            if (unlikely(tp##_is_any_nan(xa.fld) ||                     \
                         tp##_is_any_nan(xb.fld))) {                    \
                fe_flag = 1;                                            \
            } else if ((e_b <= emin) || (e_b >= (emax-2))) {            \
                fe_flag = 1;                                            \
            } else if (!tp##_is_zero(xa.fld) &&                         \
                       (((e_a - e_b) >= emax) ||                        \
                        ((e_a - e_b) <= (emin+1)) ||                    \
                         (e_a <= (emin+nbits)))) {                      \
                fe_flag = 1;                                            \
            }                                                           \
                                                                        \
            if (unlikely(tp##_is_zero_or_denormal(xb.fld))) {           \
                /* XB is not zero because of the above check and */     \
                /* so must be denormalized.                      */     \
                fg_flag = 1;                                            \
            }                                                           \
        }                                                               \
    }                                                                   \
                                                                        \
    env->crf[BF(opcode)] = 0x8 | (fg_flag ? 4 : 0) | (fe_flag ? 2 : 0); \
}

VSX_TDIV(xstdivdp, 1, float64, VsrD(0), -1022, 1023, 52)
VSX_TDIV(xvtdivdp, 2, float64, VsrD(i), -1022, 1023, 52)
VSX_TDIV(xvtdivsp, 4, float32, VsrW(i), -126, 127, 23)

/* VSX_TSQRT - VSX floating point test for square root
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   emin  - minimum unbiased exponent
 *   emax  - maximum unbiased exponent
 *   nbits - number of fraction bits
 */
#define VSX_TSQRT(op, nels, tp, fld, emin, nbits)                       \
void helper_##op(CPUPPCState *env, uint32_t opcode)                     \
{                                                                       \
    ppc_vsr_t xa, xb;                                                   \
    int i;                                                              \
    int fe_flag = 0;                                                    \
    int fg_flag = 0;                                                    \
                                                                        \
    getVSR(xA(opcode), &xa, env);                                       \
    getVSR(xB(opcode), &xb, env);                                       \
                                                                        \
    for (i = 0; i < nels; i++) {                                        \
        if (unlikely(tp##_is_infinity(xb.fld) ||                        \
                     tp##_is_zero(xb.fld))) {                           \
            fe_flag = 1;                                                \
            fg_flag = 1;                                                \
        } else {                                                        \
            int e_b = ppc_##tp##_get_unbiased_exp(xb.fld);              \
                                                                        \
            if (unlikely(tp##_is_any_nan(xb.fld))) {                    \
                fe_flag = 1;                                            \
            } else if (unlikely(tp##_is_zero(xb.fld))) {                \
                fe_flag = 1;                                            \
            } else if (unlikely(tp##_is_neg(xb.fld))) {                 \
                fe_flag = 1;                                            \
            } else if (!tp##_is_zero(xb.fld) &&                         \
                      (e_b <= (emin+nbits))) {                          \
                fe_flag = 1;                                            \
            }                                                           \
                                                                        \
            if (unlikely(tp##_is_zero_or_denormal(xb.fld))) {           \
                /* XB is not zero because of the above check and */     \
                /* therefore must be denormalized.               */     \
                fg_flag = 1;                                            \
            }                                                           \
        }                                                               \
    }                                                                   \
                                                                        \
    env->crf[BF(opcode)] = 0x8 | (fg_flag ? 4 : 0) | (fe_flag ? 2 : 0); \
}

VSX_TSQRT(xstsqrtdp, 1, float64, VsrD(0), -1022, 52)
VSX_TSQRT(xvtsqrtdp, 2, float64, VsrD(i), -1022, 52)
VSX_TSQRT(xvtsqrtsp, 4, float32, VsrW(i), -126, 23)

/* VSX_MADD - VSX floating point muliply/add variations
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   maddflgs - flags for the float*muladd routine that control the
 *           various forms (madd, msub, nmadd, nmsub)
 *   afrm  - A form (1=A, 0=M)
 *   sfprf - set FPRF
 */
#define VSX_MADD(op, nels, tp, fld, maddflgs, afrm, sfprf, r2sp)              \
void helper_##op(CPUPPCState *env, uint32_t opcode)                           \
{                                                                             \
    ppc_vsr_t xt_in, xa, xb, xt_out;                                          \
    ppc_vsr_t *b, *c;                                                         \
    int i;                                                                    \
                                                                              \
    if (afrm) { /* AxB + T */                                                 \
        b = &xb;                                                              \
        c = &xt_in;                                                           \
    } else { /* AxT + B */                                                    \
        b = &xt_in;                                                           \
        c = &xb;                                                              \
    }                                                                         \
                                                                              \
    getVSR(xA(opcode), &xa, env);                                             \
    getVSR(xB(opcode), &xb, env);                                             \
    getVSR(xT(opcode), &xt_in, env);                                          \
                                                                              \
    xt_out = xt_in;                                                           \
                                                                              \
    helper_reset_fpstatus(env);                                               \
                                                                              \
    for (i = 0; i < nels; i++) {                                              \
        float_status tstat = env->fp_status;                                  \
        set_float_exception_flags(0, &tstat);                                 \
        if (r2sp && (tstat.float_rounding_mode == float_round_nearest_even)) {\
            /* Avoid double rounding errors by rounding the intermediate */   \
            /* result to odd.                                            */   \
            set_float_rounding_mode(float_round_to_zero, &tstat);             \
            xt_out.fld = tp##_muladd(xa.fld, b->fld, c->fld,                  \
                                       maddflgs, &tstat);                     \
            xt_out.fld |= (get_float_exception_flags(&tstat) &                \
                              float_flag_inexact) != 0;                       \
        } else {                                                              \
            xt_out.fld = tp##_muladd(xa.fld, b->fld, c->fld,                  \
                                        maddflgs, &tstat);                    \
        }                                                                     \
        env->fp_status.float_exception_flags |= tstat.float_exception_flags;  \
                                                                              \
        if (unlikely(tstat.float_exception_flags & float_flag_invalid)) {     \
            if (tp##_is_signaling_nan(xa.fld) ||                              \
                tp##_is_signaling_nan(b->fld) ||                              \
                tp##_is_signaling_nan(c->fld)) {                              \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, sfprf);    \
                tstat.float_exception_flags &= ~float_flag_invalid;           \
            }                                                                 \
            if ((tp##_is_infinity(xa.fld) && tp##_is_zero(b->fld)) ||         \
                (tp##_is_zero(xa.fld) && tp##_is_infinity(b->fld))) {         \
                xt_out.fld = float64_to_##tp(fload_invalid_op_excp(env,       \
                    POWERPC_EXCP_FP_VXIMZ, sfprf), &env->fp_status);          \
                tstat.float_exception_flags &= ~float_flag_invalid;           \
            }                                                                 \
            if ((tstat.float_exception_flags & float_flag_invalid) &&         \
                ((tp##_is_infinity(xa.fld) ||                                 \
                  tp##_is_infinity(b->fld)) &&                                \
                  tp##_is_infinity(c->fld))) {                                \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI, sfprf);     \
            }                                                                 \
        }                                                                     \
                                                                              \
        if (r2sp) {                                                           \
            xt_out.fld = helper_frsp(env, xt_out.fld);                        \
        }                                                                     \
                                                                              \
        if (sfprf) {                                                          \
            helper_compute_fprf(env, xt_out.fld, sfprf);                      \
        }                                                                     \
    }                                                                         \
    putVSR(xT(opcode), &xt_out, env);                                         \
    helper_float_check_status(env);                                           \
}

#define MADD_FLGS 0
#define MSUB_FLGS float_muladd_negate_c
#define NMADD_FLGS float_muladd_negate_result
#define NMSUB_FLGS (float_muladd_negate_c | float_muladd_negate_result)

VSX_MADD(xsmaddadp, 1, float64, VsrD(0), MADD_FLGS, 1, 1, 0)
VSX_MADD(xsmaddmdp, 1, float64, VsrD(0), MADD_FLGS, 0, 1, 0)
VSX_MADD(xsmsubadp, 1, float64, VsrD(0), MSUB_FLGS, 1, 1, 0)
VSX_MADD(xsmsubmdp, 1, float64, VsrD(0), MSUB_FLGS, 0, 1, 0)
VSX_MADD(xsnmaddadp, 1, float64, VsrD(0), NMADD_FLGS, 1, 1, 0)
VSX_MADD(xsnmaddmdp, 1, float64, VsrD(0), NMADD_FLGS, 0, 1, 0)
VSX_MADD(xsnmsubadp, 1, float64, VsrD(0), NMSUB_FLGS, 1, 1, 0)
VSX_MADD(xsnmsubmdp, 1, float64, VsrD(0), NMSUB_FLGS, 0, 1, 0)

VSX_MADD(xsmaddasp, 1, float64, VsrD(0), MADD_FLGS, 1, 1, 1)
VSX_MADD(xsmaddmsp, 1, float64, VsrD(0), MADD_FLGS, 0, 1, 1)
VSX_MADD(xsmsubasp, 1, float64, VsrD(0), MSUB_FLGS, 1, 1, 1)
VSX_MADD(xsmsubmsp, 1, float64, VsrD(0), MSUB_FLGS, 0, 1, 1)
VSX_MADD(xsnmaddasp, 1, float64, VsrD(0), NMADD_FLGS, 1, 1, 1)
VSX_MADD(xsnmaddmsp, 1, float64, VsrD(0), NMADD_FLGS, 0, 1, 1)
VSX_MADD(xsnmsubasp, 1, float64, VsrD(0), NMSUB_FLGS, 1, 1, 1)
VSX_MADD(xsnmsubmsp, 1, float64, VsrD(0), NMSUB_FLGS, 0, 1, 1)

VSX_MADD(xvmaddadp, 2, float64, VsrD(i), MADD_FLGS, 1, 0, 0)
VSX_MADD(xvmaddmdp, 2, float64, VsrD(i), MADD_FLGS, 0, 0, 0)
VSX_MADD(xvmsubadp, 2, float64, VsrD(i), MSUB_FLGS, 1, 0, 0)
VSX_MADD(xvmsubmdp, 2, float64, VsrD(i), MSUB_FLGS, 0, 0, 0)
VSX_MADD(xvnmaddadp, 2, float64, VsrD(i), NMADD_FLGS, 1, 0, 0)
VSX_MADD(xvnmaddmdp, 2, float64, VsrD(i), NMADD_FLGS, 0, 0, 0)
VSX_MADD(xvnmsubadp, 2, float64, VsrD(i), NMSUB_FLGS, 1, 0, 0)
VSX_MADD(xvnmsubmdp, 2, float64, VsrD(i), NMSUB_FLGS, 0, 0, 0)

VSX_MADD(xvmaddasp, 4, float32, VsrW(i), MADD_FLGS, 1, 0, 0)
VSX_MADD(xvmaddmsp, 4, float32, VsrW(i), MADD_FLGS, 0, 0, 0)
VSX_MADD(xvmsubasp, 4, float32, VsrW(i), MSUB_FLGS, 1, 0, 0)
VSX_MADD(xvmsubmsp, 4, float32, VsrW(i), MSUB_FLGS, 0, 0, 0)
VSX_MADD(xvnmaddasp, 4, float32, VsrW(i), NMADD_FLGS, 1, 0, 0)
VSX_MADD(xvnmaddmsp, 4, float32, VsrW(i), NMADD_FLGS, 0, 0, 0)
VSX_MADD(xvnmsubasp, 4, float32, VsrW(i), NMSUB_FLGS, 1, 0, 0)
VSX_MADD(xvnmsubmsp, 4, float32, VsrW(i), NMSUB_FLGS, 0, 0, 0)

#define VSX_SCALAR_CMP(op, ordered)                                      \
void helper_##op(CPUPPCState *env, uint32_t opcode)                      \
{                                                                        \
    ppc_vsr_t xa, xb;                                                    \
    uint32_t cc = 0;                                                     \
                                                                         \
    getVSR(xA(opcode), &xa, env);                                        \
    getVSR(xB(opcode), &xb, env);                                        \
                                                                         \
    if (unlikely(float64_is_any_nan(xa.VsrD(0)) ||                       \
                 float64_is_any_nan(xb.VsrD(0)))) {                      \
        if (float64_is_signaling_nan(xa.VsrD(0)) ||                      \
            float64_is_signaling_nan(xb.VsrD(0))) {                      \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0);       \
        }                                                                \
        if (ordered) {                                                   \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXVC, 0);         \
        }                                                                \
        cc = 1;                                                          \
    } else {                                                             \
        if (float64_lt(xa.VsrD(0), xb.VsrD(0), &env->fp_status)) {       \
            cc = 8;                                                      \
        } else if (!float64_le(xa.VsrD(0), xb.VsrD(0),                   \
                               &env->fp_status)) { \
            cc = 4;                                                      \
        } else {                                                         \
            cc = 2;                                                      \
        }                                                                \
    }                                                                    \
                                                                         \
    env->fpscr &= ~(0x0F << FPSCR_FPRF);                                 \
    env->fpscr |= cc << FPSCR_FPRF;                                      \
    env->crf[BF(opcode)] = cc;                                           \
                                                                         \
    helper_float_check_status(env);                                      \
}

VSX_SCALAR_CMP(xscmpodp, 1)
VSX_SCALAR_CMP(xscmpudp, 0)

#define float64_snan_to_qnan(x) ((x) | 0x0008000000000000ULL)
#define float32_snan_to_qnan(x) ((x) | 0x00400000)

/* VSX_MAX_MIN - VSX floating point maximum/minimum
 *   name  - instruction mnemonic
 *   op    - operation (max or min)
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 */
#define VSX_MAX_MIN(name, op, nels, tp, fld)                                  \
void helper_##name(CPUPPCState *env, uint32_t opcode)                         \
{                                                                             \
    ppc_vsr_t xt, xa, xb;                                                     \
    int i;                                                                    \
                                                                              \
    getVSR(xA(opcode), &xa, env);                                             \
    getVSR(xB(opcode), &xb, env);                                             \
    getVSR(xT(opcode), &xt, env);                                             \
                                                                              \
    for (i = 0; i < nels; i++) {                                              \
        xt.fld = tp##_##op(xa.fld, xb.fld, &env->fp_status);                  \
        if (unlikely(tp##_is_signaling_nan(xa.fld) ||                         \
                     tp##_is_signaling_nan(xb.fld))) {                        \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0);            \
        }                                                                     \
    }                                                                         \
                                                                              \
    putVSR(xT(opcode), &xt, env);                                             \
    helper_float_check_status(env);                                           \
}

VSX_MAX_MIN(xsmaxdp, maxnum, 1, float64, VsrD(0))
VSX_MAX_MIN(xvmaxdp, maxnum, 2, float64, VsrD(i))
VSX_MAX_MIN(xvmaxsp, maxnum, 4, float32, VsrW(i))
VSX_MAX_MIN(xsmindp, minnum, 1, float64, VsrD(0))
VSX_MAX_MIN(xvmindp, minnum, 2, float64, VsrD(i))
VSX_MAX_MIN(xvminsp, minnum, 4, float32, VsrW(i))

/* VSX_CMP - VSX floating point compare
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   cmp   - comparison operation
 *   svxvc - set VXVC bit
 */
#define VSX_CMP(op, nels, tp, fld, cmp, svxvc)                            \
void helper_##op(CPUPPCState *env, uint32_t opcode)                       \
{                                                                         \
    ppc_vsr_t xt, xa, xb;                                                 \
    int i;                                                                \
    int all_true = 1;                                                     \
    int all_false = 1;                                                    \
                                                                          \
    getVSR(xA(opcode), &xa, env);                                         \
    getVSR(xB(opcode), &xb, env);                                         \
    getVSR(xT(opcode), &xt, env);                                         \
                                                                          \
    for (i = 0; i < nels; i++) {                                          \
        if (unlikely(tp##_is_any_nan(xa.fld) ||                           \
                     tp##_is_any_nan(xb.fld))) {                          \
            if (tp##_is_signaling_nan(xa.fld) ||                          \
                tp##_is_signaling_nan(xb.fld)) {                          \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0);    \
            }                                                             \
            if (svxvc) {                                                  \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXVC, 0);      \
            }                                                             \
            xt.fld = 0;                                                   \
            all_true = 0;                                                 \
        } else {                                                          \
            if (tp##_##cmp(xb.fld, xa.fld, &env->fp_status) == 1) {       \
                xt.fld = -1;                                              \
                all_false = 0;                                            \
            } else {                                                      \
                xt.fld = 0;                                               \
                all_true = 0;                                             \
            }                                                             \
        }                                                                 \
    }                                                                     \
                                                                          \
    putVSR(xT(opcode), &xt, env);                                         \
    if ((opcode >> (31-21)) & 1) {                                        \
        env->crf[6] = (all_true ? 0x8 : 0) | (all_false ? 0x2 : 0);       \
    }                                                                     \
    helper_float_check_status(env);                                       \
 }

VSX_CMP(xvcmpeqdp, 2, float64, VsrD(i), eq, 0)
VSX_CMP(xvcmpgedp, 2, float64, VsrD(i), le, 1)
VSX_CMP(xvcmpgtdp, 2, float64, VsrD(i), lt, 1)
VSX_CMP(xvcmpeqsp, 4, float32, VsrW(i), eq, 0)
VSX_CMP(xvcmpgesp, 4, float32, VsrW(i), le, 1)
VSX_CMP(xvcmpgtsp, 4, float32, VsrW(i), lt, 1)

/* VSX_CVT_FP_TO_FP - VSX floating point/floating point conversion
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   stp   - source type (float32 or float64)
 *   ttp   - target type (float32 or float64)
 *   sfld  - source vsr_t field
 *   tfld  - target vsr_t field (f32 or f64)
 *   sfprf - set FPRF
 */
#define VSX_CVT_FP_TO_FP(op, nels, stp, ttp, sfld, tfld, sfprf)    \
void helper_##op(CPUPPCState *env, uint32_t opcode)                \
{                                                                  \
    ppc_vsr_t xt, xb;                                              \
    int i;                                                         \
                                                                   \
    getVSR(xB(opcode), &xb, env);                                  \
    getVSR(xT(opcode), &xt, env);                                  \
                                                                   \
    for (i = 0; i < nels; i++) {                                   \
        xt.tfld = stp##_to_##ttp(xb.sfld, &env->fp_status);        \
        if (unlikely(stp##_is_signaling_nan(xb.sfld))) {           \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0); \
            xt.tfld = ttp##_snan_to_qnan(xt.tfld);                 \
        }                                                          \
        if (sfprf) {                                               \
            helper_compute_fprf(env, ttp##_to_float64(xt.tfld,     \
                                &env->fp_status), sfprf);          \
        }                                                          \
    }                                                              \
                                                                   \
    putVSR(xT(opcode), &xt, env);                                  \
    helper_float_check_status(env);                                \
}

VSX_CVT_FP_TO_FP(xscvdpsp, 1, float64, float32, VsrD(0), VsrW(0), 1)
VSX_CVT_FP_TO_FP(xscvspdp, 1, float32, float64, VsrW(0), VsrD(0), 1)
VSX_CVT_FP_TO_FP(xvcvdpsp, 2, float64, float32, VsrD(i), VsrW(2*i), 0)
VSX_CVT_FP_TO_FP(xvcvspdp, 2, float32, float64, VsrW(2*i), VsrD(i), 0)

uint64_t helper_xscvdpspn(CPUPPCState *env, uint64_t xb)
{
    float_status tstat = env->fp_status;
    set_float_exception_flags(0, &tstat);

    return (uint64_t)float64_to_float32(xb, &tstat) << 32;
}

uint64_t helper_xscvspdpn(CPUPPCState *env, uint64_t xb)
{
    float_status tstat = env->fp_status;
    set_float_exception_flags(0, &tstat);

    return float32_to_float64(xb >> 32, &tstat);
}

/* VSX_CVT_FP_TO_INT - VSX floating point to integer conversion
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   stp   - source type (float32 or float64)
 *   ttp   - target type (int32, uint32, int64 or uint64)
 *   sfld  - source vsr_t field
 *   tfld  - target vsr_t field
 *   rnan  - resulting NaN
 */
#define VSX_CVT_FP_TO_INT(op, nels, stp, ttp, sfld, tfld, rnan)              \
void helper_##op(CPUPPCState *env, uint32_t opcode)                          \
{                                                                            \
    ppc_vsr_t xt, xb;                                                        \
    int i;                                                                   \
                                                                             \
    getVSR(xB(opcode), &xb, env);                                            \
    getVSR(xT(opcode), &xt, env);                                            \
                                                                             \
    for (i = 0; i < nels; i++) {                                             \
        if (unlikely(stp##_is_any_nan(xb.sfld))) {                           \
            if (stp##_is_signaling_nan(xb.sfld)) {                           \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0);       \
            }                                                                \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI, 0);            \
            xt.tfld = rnan;                                                  \
        } else {                                                             \
            xt.tfld = stp##_to_##ttp##_round_to_zero(xb.sfld,                \
                          &env->fp_status);                                  \
            if (env->fp_status.float_exception_flags & float_flag_invalid) { \
                fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI, 0);        \
            }                                                                \
        }                                                                    \
    }                                                                        \
                                                                             \
    putVSR(xT(opcode), &xt, env);                                            \
    helper_float_check_status(env);                                          \
}

VSX_CVT_FP_TO_INT(xscvdpsxds, 1, float64, int64, VsrD(0), VsrD(0), \
                  0x8000000000000000ULL)
VSX_CVT_FP_TO_INT(xscvdpsxws, 1, float64, int32, VsrD(0), VsrW(1), \
                  0x80000000U)
VSX_CVT_FP_TO_INT(xscvdpuxds, 1, float64, uint64, VsrD(0), VsrD(0), 0ULL)
VSX_CVT_FP_TO_INT(xscvdpuxws, 1, float64, uint32, VsrD(0), VsrW(1), 0U)
VSX_CVT_FP_TO_INT(xvcvdpsxds, 2, float64, int64, VsrD(i), VsrD(i), \
                  0x8000000000000000ULL)
VSX_CVT_FP_TO_INT(xvcvdpsxws, 2, float64, int32, VsrD(i), VsrW(2*i), \
                  0x80000000U)
VSX_CVT_FP_TO_INT(xvcvdpuxds, 2, float64, uint64, VsrD(i), VsrD(i), 0ULL)
VSX_CVT_FP_TO_INT(xvcvdpuxws, 2, float64, uint32, VsrD(i), VsrW(2*i), 0U)
VSX_CVT_FP_TO_INT(xvcvspsxds, 2, float32, int64, VsrW(2*i), VsrD(i), \
                  0x8000000000000000ULL)
VSX_CVT_FP_TO_INT(xvcvspsxws, 4, float32, int32, VsrW(i), VsrW(i), 0x80000000U)
VSX_CVT_FP_TO_INT(xvcvspuxds, 2, float32, uint64, VsrW(2*i), VsrD(i), 0ULL)
VSX_CVT_FP_TO_INT(xvcvspuxws, 4, float32, uint32, VsrW(i), VsrW(i), 0U)

/* VSX_CVT_INT_TO_FP - VSX integer to floating point conversion
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   stp   - source type (int32, uint32, int64 or uint64)
 *   ttp   - target type (float32 or float64)
 *   sfld  - source vsr_t field
 *   tfld  - target vsr_t field
 *   jdef  - definition of the j index (i or 2*i)
 *   sfprf - set FPRF
 */
#define VSX_CVT_INT_TO_FP(op, nels, stp, ttp, sfld, tfld, sfprf, r2sp)  \
void helper_##op(CPUPPCState *env, uint32_t opcode)                     \
{                                                                       \
    ppc_vsr_t xt, xb;                                                   \
    int i;                                                              \
                                                                        \
    getVSR(xB(opcode), &xb, env);                                       \
    getVSR(xT(opcode), &xt, env);                                       \
                                                                        \
    for (i = 0; i < nels; i++) {                                        \
        xt.tfld = stp##_to_##ttp(xb.sfld, &env->fp_status);             \
        if (r2sp) {                                                     \
            xt.tfld = helper_frsp(env, xt.tfld);                        \
        }                                                               \
        if (sfprf) {                                                    \
            helper_compute_fprf(env, xt.tfld, sfprf);                   \
        }                                                               \
    }                                                                   \
                                                                        \
    putVSR(xT(opcode), &xt, env);                                       \
    helper_float_check_status(env);                                     \
}

VSX_CVT_INT_TO_FP(xscvsxddp, 1, int64, float64, VsrD(0), VsrD(0), 1, 0)
VSX_CVT_INT_TO_FP(xscvuxddp, 1, uint64, float64, VsrD(0), VsrD(0), 1, 0)
VSX_CVT_INT_TO_FP(xscvsxdsp, 1, int64, float64, VsrD(0), VsrD(0), 1, 1)
VSX_CVT_INT_TO_FP(xscvuxdsp, 1, uint64, float64, VsrD(0), VsrD(0), 1, 1)
VSX_CVT_INT_TO_FP(xvcvsxddp, 2, int64, float64, VsrD(i), VsrD(i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvuxddp, 2, uint64, float64, VsrD(i), VsrD(i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvsxwdp, 2, int32, float64, VsrW(2*i), VsrD(i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvuxwdp, 2, uint64, float64, VsrW(2*i), VsrD(i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvsxdsp, 2, int64, float32, VsrD(i), VsrW(2*i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvuxdsp, 2, uint64, float32, VsrD(i), VsrW(2*i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvsxwsp, 4, int32, float32, VsrW(i), VsrW(i), 0, 0)
VSX_CVT_INT_TO_FP(xvcvuxwsp, 4, uint32, float32, VsrW(i), VsrW(i), 0, 0)

/* For "use current rounding mode", define a value that will not be one of
 * the existing rounding model enums.
 */
#define FLOAT_ROUND_CURRENT (float_round_nearest_even + float_round_down + \
  float_round_up + float_round_to_zero)

/* VSX_ROUND - VSX floating point round
 *   op    - instruction mnemonic
 *   nels  - number of elements (1, 2 or 4)
 *   tp    - type (float32 or float64)
 *   fld   - vsr_t field (VsrD(*) or VsrW(*))
 *   rmode - rounding mode
 *   sfprf - set FPRF
 */
#define VSX_ROUND(op, nels, tp, fld, rmode, sfprf)                     \
void helper_##op(CPUPPCState *env, uint32_t opcode)                    \
{                                                                      \
    ppc_vsr_t xt, xb;                                                  \
    int i;                                                             \
    getVSR(xB(opcode), &xb, env);                                      \
    getVSR(xT(opcode), &xt, env);                                      \
                                                                       \
    if (rmode != FLOAT_ROUND_CURRENT) {                                \
        set_float_rounding_mode(rmode, &env->fp_status);               \
    }                                                                  \
                                                                       \
    for (i = 0; i < nels; i++) {                                       \
        if (unlikely(tp##_is_signaling_nan(xb.fld))) {                 \
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN, 0);     \
            xt.fld = tp##_snan_to_qnan(xb.fld);                        \
        } else {                                                       \
            xt.fld = tp##_round_to_int(xb.fld, &env->fp_status);       \
        }                                                              \
        if (sfprf) {                                                   \
            helper_compute_fprf(env, xt.fld, sfprf);                   \
        }                                                              \
    }                                                                  \
                                                                       \
    /* If this is not a "use current rounding mode" instruction,       \
     * then inhibit setting of the XX bit and restore rounding         \
     * mode from FPSCR */                                              \
    if (rmode != FLOAT_ROUND_CURRENT) {                                \
        fpscr_set_rounding_mode(env);                                  \
        env->fp_status.float_exception_flags &= ~float_flag_inexact;   \
    }                                                                  \
                                                                       \
    putVSR(xT(opcode), &xt, env);                                      \
    helper_float_check_status(env);                                    \
}

VSX_ROUND(xsrdpi, 1, float64, VsrD(0), float_round_nearest_even, 1)
VSX_ROUND(xsrdpic, 1, float64, VsrD(0), FLOAT_ROUND_CURRENT, 1)
VSX_ROUND(xsrdpim, 1, float64, VsrD(0), float_round_down, 1)
VSX_ROUND(xsrdpip, 1, float64, VsrD(0), float_round_up, 1)
VSX_ROUND(xsrdpiz, 1, float64, VsrD(0), float_round_to_zero, 1)

VSX_ROUND(xvrdpi, 2, float64, VsrD(i), float_round_nearest_even, 0)
VSX_ROUND(xvrdpic, 2, float64, VsrD(i), FLOAT_ROUND_CURRENT, 0)
VSX_ROUND(xvrdpim, 2, float64, VsrD(i), float_round_down, 0)
VSX_ROUND(xvrdpip, 2, float64, VsrD(i), float_round_up, 0)
VSX_ROUND(xvrdpiz, 2, float64, VsrD(i), float_round_to_zero, 0)

VSX_ROUND(xvrspi, 4, float32, VsrW(i), float_round_nearest_even, 0)
VSX_ROUND(xvrspic, 4, float32, VsrW(i), FLOAT_ROUND_CURRENT, 0)
VSX_ROUND(xvrspim, 4, float32, VsrW(i), float_round_down, 0)
VSX_ROUND(xvrspip, 4, float32, VsrW(i), float_round_up, 0)
VSX_ROUND(xvrspiz, 4, float32, VsrW(i), float_round_to_zero, 0)

uint64_t helper_xsrsp(CPUPPCState *env, uint64_t xb)
{
    helper_reset_fpstatus(env);

    uint64_t xt = helper_frsp(env, xb);

    helper_compute_fprf(env, xt, 1);
    helper_float_check_status(env);
    return xt;
}
