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
static inline uint64_t fload_invalid_op_excp(CPUPPCState *env, int op)
{
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
        env->fpscr &= ~(0xF << FPSCR_FPCC);
        env->fpscr |= 0x11 << FPSCR_FPCC;
        /* We must update the target FPR before raising the exception */
        if (ve != 0) {
            env->exception_index = POWERPC_EXCP_PROGRAM;
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
            env->fpscr &= ~(0xF << FPSCR_FPCC);
            env->fpscr |= 0x11 << FPSCR_FPCC;
        }
        break;
    case POWERPC_EXCP_FP_VXCVI:
        /* Invalid conversion */
        env->fpscr |= 1 << FPSCR_VXCVI;
        env->fpscr &= ~((1 << FPSCR_FR) | (1 << FPSCR_FI));
        if (ve == 0) {
            /* Set the result to quiet NaN */
            ret = 0x7FF8000000000000ULL;
            env->fpscr &= ~(0xF << FPSCR_FPCC);
            env->fpscr |= 0x11 << FPSCR_FPCC;
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
    env->fpscr |= 1 << FPSCR_OX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_oe != 0) {
        /* XXX: should adjust the result */
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        env->exception_index = POWERPC_EXCP_PROGRAM;
        env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_OX;
    } else {
        env->fpscr |= 1 << FPSCR_XX;
        env->fpscr |= 1 << FPSCR_FI;
    }
}

static inline void float_underflow_excp(CPUPPCState *env)
{
    env->fpscr |= 1 << FPSCR_UX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_ue != 0) {
        /* XXX: should adjust the result */
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        env->exception_index = POWERPC_EXCP_PROGRAM;
        env->error_code = POWERPC_EXCP_FP | POWERPC_EXCP_FP_UX;
    }
}

static inline void float_inexact_excp(CPUPPCState *env)
{
    env->fpscr |= 1 << FPSCR_XX;
    /* Update the floating-point exception summary */
    env->fpscr |= 1 << FPSCR_FX;
    if (fpscr_xe != 0) {
        /* Update the floating-point enabled exception summary */
        env->fpscr |= 1 << FPSCR_FEX;
        /* We must update the target FPR before raising the exception */
        env->exception_index = POWERPC_EXCP_PROGRAM;
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
            env->exception_index = POWERPC_EXCP_PROGRAM;
            break;
        }
    }
}

void helper_store_fpscr(CPUPPCState *env, uint64_t arg, uint32_t mask)
{
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
        env->exception_index = POWERPC_EXCP_PROGRAM;
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

    if (env->exception_index == POWERPC_EXCP_PROGRAM &&
        (env->error_code & POWERPC_EXCP_FP)) {
        /* Differred floating-point exception after target FPR update */
        if (msr_fe0 != 0 || msr_fe1 != 0) {
            helper_raise_exception_err(env, env->exception_index,
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN addition */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN subtraction */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN multiplication */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIDI);
    } else if (unlikely(float64_is_zero(farg1.d) && float64_is_zero(farg2.d))) {
        /* Division of zero by zero */
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXZDZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d))) {
            /* sNaN division */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
        }
        farg1.d = float64_div(farg1.d, farg2.d, &env->fp_status);
    }

    return farg1.ll;
}

/* fctiw - fctiw. */
uint64_t helper_fctiw(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                        POWERPC_EXCP_FP_VXCVI);
    } else if (unlikely(float64_is_quiet_nan(farg.d) ||
                        float64_is_infinity(farg.d))) {
        /* qNan / infinity conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI);
    } else {
        farg.ll = float64_to_int32(farg.d, &env->fp_status);
        /* XXX: higher bits are not supposed to be significant.
         *     to make tests easier, return the same as a real PowerPC 750
         */
        farg.ll |= 0xFFF80000ULL << 32;
    }
    return farg.ll;
}

/* fctiwz - fctiwz. */
uint64_t helper_fctiwz(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                        POWERPC_EXCP_FP_VXCVI);
    } else if (unlikely(float64_is_quiet_nan(farg.d) ||
                        float64_is_infinity(farg.d))) {
        /* qNan / infinity conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI);
    } else {
        farg.ll = float64_to_int32_round_to_zero(farg.d, &env->fp_status);
        /* XXX: higher bits are not supposed to be significant.
         *     to make tests easier, return the same as a real PowerPC 750
         */
        farg.ll |= 0xFFF80000ULL << 32;
    }
    return farg.ll;
}

#if defined(TARGET_PPC64)
/* fcfid - fcfid. */
uint64_t helper_fcfid(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.d = int64_to_float64(arg, &env->fp_status);
    return farg.ll;
}

/* fctid - fctid. */
uint64_t helper_fctid(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                        POWERPC_EXCP_FP_VXCVI);
    } else if (unlikely(float64_is_quiet_nan(farg.d) ||
                        float64_is_infinity(farg.d))) {
        /* qNan / infinity conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI);
    } else {
        farg.ll = float64_to_int64(farg.d, &env->fp_status);
    }
    return farg.ll;
}

/* fctidz - fctidz. */
uint64_t helper_fctidz(CPUPPCState *env, uint64_t arg)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                        POWERPC_EXCP_FP_VXCVI);
    } else if (unlikely(float64_is_quiet_nan(farg.d) ||
                        float64_is_infinity(farg.d))) {
        /* qNan / infinity conversion */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI);
    } else {
        farg.ll = float64_to_int64_round_to_zero(farg.d, &env->fp_status);
    }
    return farg.ll;
}

#endif

static inline uint64_t do_fri(CPUPPCState *env, uint64_t arg,
                              int rounding_mode)
{
    CPU_DoubleU farg;

    farg.ll = arg;

    if (unlikely(float64_is_signaling_nan(farg.d))) {
        /* sNaN round */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN |
                                        POWERPC_EXCP_FP_VXCVI);
    } else if (unlikely(float64_is_quiet_nan(farg.d) ||
                        float64_is_infinity(farg.d))) {
        /* qNan / infinity round */
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXCVI);
    } else {
        set_float_rounding_mode(rounding_mode, &env->fp_status);
        farg.ll = float64_round_to_int(farg.d, &env->fp_status);
        /* Restore rounding mode from FPSCR */
        fpscr_set_rounding_mode(env);
    }
    return farg.ll;
}

uint64_t helper_frin(CPUPPCState *env, uint64_t arg)
{
    return do_fri(env, arg, float_round_nearest_even);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
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
        farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXIMZ);
    } else {
        if (unlikely(float64_is_signaling_nan(farg1.d) ||
                     float64_is_signaling_nan(farg2.d) ||
                     float64_is_signaling_nan(farg3.d))) {
            /* sNaN operation */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
            farg1.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXISI);
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
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT);
    } else {
        if (unlikely(float64_is_signaling_nan(farg.d))) {
            /* sNaN square root */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        farg.ll = fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSQRT);
    } else {
        if (unlikely(float64_is_signaling_nan(farg.d))) {
            /* sNaN reciprocal square root */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
        fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXSNAN);
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
                                  POWERPC_EXCP_FP_VXVC);
        } else {
            /* qNaN comparison */
            fload_invalid_op_excp(env, POWERPC_EXCP_FP_VXVC);
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
