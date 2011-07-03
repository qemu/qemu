/*
 * FPU op helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

#define DT0 (env->dt0)
#define DT1 (env->dt1)
#define QT0 (env->qt0)
#define QT1 (env->qt1)

#define F_HELPER(name, p) void helper_f##name##p(CPUState *env)

#define F_BINOP(name)                                           \
    float32 helper_f ## name ## s (CPUState * env, float32 src1,\
                                   float32 src2)                \
    {                                                           \
        return float32_ ## name (src1, src2, &env->fp_status);  \
    }                                                           \
    F_HELPER(name, d)                                           \
    {                                                           \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
    }                                                           \
    F_HELPER(name, q)                                           \
    {                                                           \
        QT0 = float128_ ## name (QT0, QT1, &env->fp_status);    \
    }

F_BINOP(add);
F_BINOP(sub);
F_BINOP(mul);
F_BINOP(div);
#undef F_BINOP

void helper_fsmuld(CPUState *env, float32 src1, float32 src2)
{
    DT0 = float64_mul(float32_to_float64(src1, &env->fp_status),
                      float32_to_float64(src2, &env->fp_status),
                      &env->fp_status);
}

void helper_fdmulq(CPUState *env)
{
    QT0 = float128_mul(float64_to_float128(DT0, &env->fp_status),
                       float64_to_float128(DT1, &env->fp_status),
                       &env->fp_status);
}

float32 helper_fnegs(float32 src)
{
    return float32_chs(src);
}

#ifdef TARGET_SPARC64
F_HELPER(neg, d)
{
    DT0 = float64_chs(DT1);
}

F_HELPER(neg, q)
{
    QT0 = float128_chs(QT1);
}
#endif

/* Integer to float conversion.  */
float32 helper_fitos(CPUState *env, int32_t src)
{
    return int32_to_float32(src, &env->fp_status);
}

void helper_fitod(CPUState *env, int32_t src)
{
    DT0 = int32_to_float64(src, &env->fp_status);
}

void helper_fitoq(CPUState *env, int32_t src)
{
    QT0 = int32_to_float128(src, &env->fp_status);
}

#ifdef TARGET_SPARC64
float32 helper_fxtos(CPUState *env)
{
    return int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
}

F_HELPER(xto, d)
{
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
}

F_HELPER(xto, q)
{
    QT0 = int64_to_float128(*((int64_t *)&DT1), &env->fp_status);
}
#endif
#undef F_HELPER

/* floating point conversion */
float32 helper_fdtos(CPUState *env)
{
    return float64_to_float32(DT1, &env->fp_status);
}

void helper_fstod(CPUState *env, float32 src)
{
    DT0 = float32_to_float64(src, &env->fp_status);
}

float32 helper_fqtos(CPUState *env)
{
    return float128_to_float32(QT1, &env->fp_status);
}

void helper_fstoq(CPUState *env, float32 src)
{
    QT0 = float32_to_float128(src, &env->fp_status);
}

void helper_fqtod(CPUState *env)
{
    DT0 = float128_to_float64(QT1, &env->fp_status);
}

void helper_fdtoq(CPUState *env)
{
    QT0 = float64_to_float128(DT1, &env->fp_status);
}

/* Float to integer conversion.  */
int32_t helper_fstoi(CPUState *env, float32 src)
{
    return float32_to_int32_round_to_zero(src, &env->fp_status);
}

int32_t helper_fdtoi(CPUState *env)
{
    return float64_to_int32_round_to_zero(DT1, &env->fp_status);
}

int32_t helper_fqtoi(CPUState *env)
{
    return float128_to_int32_round_to_zero(QT1, &env->fp_status);
}

#ifdef TARGET_SPARC64
void helper_fstox(CPUState *env, float32 src)
{
    *((int64_t *)&DT0) = float32_to_int64_round_to_zero(src, &env->fp_status);
}

void helper_fdtox(CPUState *env)
{
    *((int64_t *)&DT0) = float64_to_int64_round_to_zero(DT1, &env->fp_status);
}

void helper_fqtox(CPUState *env)
{
    *((int64_t *)&DT0) = float128_to_int64_round_to_zero(QT1, &env->fp_status);
}
#endif

float32 helper_fabss(float32 src)
{
    return float32_abs(src);
}

#ifdef TARGET_SPARC64
void helper_fabsd(CPUState *env)
{
    DT0 = float64_abs(DT1);
}

void helper_fabsq(CPUState *env)
{
    QT0 = float128_abs(QT1);
}
#endif

float32 helper_fsqrts(CPUState *env, float32 src)
{
    return float32_sqrt(src, &env->fp_status);
}

void helper_fsqrtd(CPUState *env)
{
    DT0 = float64_sqrt(DT1, &env->fp_status);
}

void helper_fsqrtq(CPUState *env)
{
    QT0 = float128_sqrt(QT1, &env->fp_status);
}

#define GEN_FCMP(name, size, reg1, reg2, FS, E)                         \
    void glue(helper_, name) (CPUState *env)                            \
    {                                                                   \
        env->fsr &= FSR_FTT_NMASK;                                      \
        if (E && (glue(size, _is_any_nan)(reg1) ||                      \
                  glue(size, _is_any_nan)(reg2)) &&                     \
            (env->fsr & FSR_NVM)) {                                     \
            env->fsr |= FSR_NVC;                                        \
            env->fsr |= FSR_FTT_IEEE_EXCP;                              \
            helper_raise_exception(env, TT_FP_EXCP);                    \
        }                                                               \
        switch (glue(size, _compare) (reg1, reg2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            if ((env->fsr & FSR_NVM)) {                                 \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                helper_raise_exception(env, TT_FP_EXCP);                \
            } else {                                                    \
                env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);             \
                env->fsr |= (FSR_FCC1 | FSR_FCC0) << FS;                \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            env->fsr |= FSR_FCC0 << FS;                                 \
            break;                                                      \
        case float_relation_greater:                                    \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            env->fsr |= FSR_FCC1 << FS;                                 \
            break;                                                      \
        default:                                                        \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            break;                                                      \
        }                                                               \
    }
#define GEN_FCMPS(name, size, FS, E)                                    \
    void glue(helper_, name)(CPUState *env, float32 src1, float32 src2) \
    {                                                                   \
        env->fsr &= FSR_FTT_NMASK;                                      \
        if (E && (glue(size, _is_any_nan)(src1) ||                      \
                  glue(size, _is_any_nan)(src2)) &&                     \
            (env->fsr & FSR_NVM)) {                                     \
            env->fsr |= FSR_NVC;                                        \
            env->fsr |= FSR_FTT_IEEE_EXCP;                              \
            helper_raise_exception(env, TT_FP_EXCP);                    \
        }                                                               \
        switch (glue(size, _compare) (src1, src2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            if ((env->fsr & FSR_NVM)) {                                 \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                helper_raise_exception(env, TT_FP_EXCP);                \
            } else {                                                    \
                env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);             \
                env->fsr |= (FSR_FCC1 | FSR_FCC0) << FS;                \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            env->fsr |= FSR_FCC0 << FS;                                 \
            break;                                                      \
        case float_relation_greater:                                    \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            env->fsr |= FSR_FCC1 << FS;                                 \
            break;                                                      \
        default:                                                        \
            env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                 \
            break;                                                      \
        }                                                               \
    }

GEN_FCMPS(fcmps, float32, 0, 0);
GEN_FCMP(fcmpd, float64, DT0, DT1, 0, 0);

GEN_FCMPS(fcmpes, float32, 0, 1);
GEN_FCMP(fcmped, float64, DT0, DT1, 0, 1);

GEN_FCMP(fcmpq, float128, QT0, QT1, 0, 0);
GEN_FCMP(fcmpeq, float128, QT0, QT1, 0, 1);

#ifdef TARGET_SPARC64
GEN_FCMPS(fcmps_fcc1, float32, 22, 0);
GEN_FCMP(fcmpd_fcc1, float64, DT0, DT1, 22, 0);
GEN_FCMP(fcmpq_fcc1, float128, QT0, QT1, 22, 0);

GEN_FCMPS(fcmps_fcc2, float32, 24, 0);
GEN_FCMP(fcmpd_fcc2, float64, DT0, DT1, 24, 0);
GEN_FCMP(fcmpq_fcc2, float128, QT0, QT1, 24, 0);

GEN_FCMPS(fcmps_fcc3, float32, 26, 0);
GEN_FCMP(fcmpd_fcc3, float64, DT0, DT1, 26, 0);
GEN_FCMP(fcmpq_fcc3, float128, QT0, QT1, 26, 0);

GEN_FCMPS(fcmpes_fcc1, float32, 22, 1);
GEN_FCMP(fcmped_fcc1, float64, DT0, DT1, 22, 1);
GEN_FCMP(fcmpeq_fcc1, float128, QT0, QT1, 22, 1);

GEN_FCMPS(fcmpes_fcc2, float32, 24, 1);
GEN_FCMP(fcmped_fcc2, float64, DT0, DT1, 24, 1);
GEN_FCMP(fcmpeq_fcc2, float128, QT0, QT1, 24, 1);

GEN_FCMPS(fcmpes_fcc3, float32, 26, 1);
GEN_FCMP(fcmped_fcc3, float64, DT0, DT1, 26, 1);
GEN_FCMP(fcmpeq_fcc3, float128, QT0, QT1, 26, 1);
#endif
#undef GEN_FCMPS

void helper_check_ieee_exceptions(CPUState *env)
{
    target_ulong status;

    status = get_float_exception_flags(&env->fp_status);
    if (status) {
        /* Copy IEEE 754 flags into FSR */
        if (status & float_flag_invalid) {
            env->fsr |= FSR_NVC;
        }
        if (status & float_flag_overflow) {
            env->fsr |= FSR_OFC;
        }
        if (status & float_flag_underflow) {
            env->fsr |= FSR_UFC;
        }
        if (status & float_flag_divbyzero) {
            env->fsr |= FSR_DZC;
        }
        if (status & float_flag_inexact) {
            env->fsr |= FSR_NXC;
        }

        if ((env->fsr & FSR_CEXC_MASK) & ((env->fsr & FSR_TEM_MASK) >> 23)) {
            /* Unmasked exception, generate a trap */
            env->fsr |= FSR_FTT_IEEE_EXCP;
            helper_raise_exception(env, TT_FP_EXCP);
        } else {
            /* Accumulate exceptions */
            env->fsr |= (env->fsr & FSR_CEXC_MASK) << 5;
        }
    }
}

void helper_clear_float_exceptions(CPUState *env)
{
    set_float_exception_flags(0, &env->fp_status);
}

static inline void set_fsr(CPUState *env)
{
    int rnd_mode;

    switch (env->fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
        rnd_mode = float_round_nearest_even;
        break;
    default:
    case FSR_RD_ZERO:
        rnd_mode = float_round_to_zero;
        break;
    case FSR_RD_POS:
        rnd_mode = float_round_up;
        break;
    case FSR_RD_NEG:
        rnd_mode = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_mode, &env->fp_status);
}

void helper_ldfsr(CPUState *env, uint32_t new_fsr)
{
    env->fsr = (new_fsr & FSR_LDFSR_MASK) | (env->fsr & FSR_LDFSR_OLDMASK);
    set_fsr(env);
}

#ifdef TARGET_SPARC64
void helper_ldxfsr(CPUState *env, uint64_t new_fsr)
{
    env->fsr = (new_fsr & FSR_LDXFSR_MASK) | (env->fsr & FSR_LDXFSR_OLDMASK);
    set_fsr(env);
}
#endif
