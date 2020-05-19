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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#define QT0 (env->qt0)
#define QT1 (env->qt1)

static target_ulong do_check_ieee_exceptions(CPUSPARCState *env, uintptr_t ra)
{
    target_ulong status = get_float_exception_flags(&env->fp_status);
    target_ulong fsr = env->fsr;

    if (unlikely(status)) {
        /* Keep exception flags clear for next time.  */
        set_float_exception_flags(0, &env->fp_status);

        /* Copy IEEE 754 flags into FSR */
        if (status & float_flag_invalid) {
            fsr |= FSR_NVC;
        }
        if (status & float_flag_overflow) {
            fsr |= FSR_OFC;
        }
        if (status & float_flag_underflow) {
            fsr |= FSR_UFC;
        }
        if (status & float_flag_divbyzero) {
            fsr |= FSR_DZC;
        }
        if (status & float_flag_inexact) {
            fsr |= FSR_NXC;
        }

        if ((fsr & FSR_CEXC_MASK) & ((fsr & FSR_TEM_MASK) >> 23)) {
            CPUState *cs = env_cpu(env);

            /* Unmasked exception, generate a trap.  Note that while
               the helper is marked as NO_WG, we can get away with
               writing to cpu state along the exception path, since
               TCG generated code will never see the write.  */
            env->fsr = fsr | FSR_FTT_IEEE_EXCP;
            cs->exception_index = TT_FP_EXCP;
            cpu_loop_exit_restore(cs, ra);
        } else {
            /* Accumulate exceptions */
            fsr |= (fsr & FSR_CEXC_MASK) << 5;
        }
    }

    return fsr;
}

target_ulong helper_check_ieee_exceptions(CPUSPARCState *env)
{
    return do_check_ieee_exceptions(env, GETPC());
}

#define F_HELPER(name, p) void helper_f##name##p(CPUSPARCState *env)

#define F_BINOP(name)                                           \
    float32 helper_f ## name ## s (CPUSPARCState *env, float32 src1, \
                                   float32 src2)                \
    {                                                           \
        return float32_ ## name (src1, src2, &env->fp_status);  \
    }                                                           \
    float64 helper_f ## name ## d (CPUSPARCState * env, float64 src1,\
                                   float64 src2)                \
    {                                                           \
        return float64_ ## name (src1, src2, &env->fp_status);  \
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

float64 helper_fsmuld(CPUSPARCState *env, float32 src1, float32 src2)
{
    return float64_mul(float32_to_float64(src1, &env->fp_status),
                       float32_to_float64(src2, &env->fp_status),
                       &env->fp_status);
}

void helper_fdmulq(CPUSPARCState *env, float64 src1, float64 src2)
{
    QT0 = float128_mul(float64_to_float128(src1, &env->fp_status),
                       float64_to_float128(src2, &env->fp_status),
                       &env->fp_status);
}

float32 helper_fnegs(float32 src)
{
    return float32_chs(src);
}

#ifdef TARGET_SPARC64
float64 helper_fnegd(float64 src)
{
    return float64_chs(src);
}

F_HELPER(neg, q)
{
    QT0 = float128_chs(QT1);
}
#endif

/* Integer to float conversion.  */
float32 helper_fitos(CPUSPARCState *env, int32_t src)
{
    return int32_to_float32(src, &env->fp_status);
}

float64 helper_fitod(CPUSPARCState *env, int32_t src)
{
    return int32_to_float64(src, &env->fp_status);
}

void helper_fitoq(CPUSPARCState *env, int32_t src)
{
    QT0 = int32_to_float128(src, &env->fp_status);
}

#ifdef TARGET_SPARC64
float32 helper_fxtos(CPUSPARCState *env, int64_t src)
{
    return int64_to_float32(src, &env->fp_status);
}

float64 helper_fxtod(CPUSPARCState *env, int64_t src)
{
    return int64_to_float64(src, &env->fp_status);
}

void helper_fxtoq(CPUSPARCState *env, int64_t src)
{
    QT0 = int64_to_float128(src, &env->fp_status);
}
#endif
#undef F_HELPER

/* floating point conversion */
float32 helper_fdtos(CPUSPARCState *env, float64 src)
{
    return float64_to_float32(src, &env->fp_status);
}

float64 helper_fstod(CPUSPARCState *env, float32 src)
{
    return float32_to_float64(src, &env->fp_status);
}

float32 helper_fqtos(CPUSPARCState *env)
{
    return float128_to_float32(QT1, &env->fp_status);
}

void helper_fstoq(CPUSPARCState *env, float32 src)
{
    QT0 = float32_to_float128(src, &env->fp_status);
}

float64 helper_fqtod(CPUSPARCState *env)
{
    return float128_to_float64(QT1, &env->fp_status);
}

void helper_fdtoq(CPUSPARCState *env, float64 src)
{
    QT0 = float64_to_float128(src, &env->fp_status);
}

/* Float to integer conversion.  */
int32_t helper_fstoi(CPUSPARCState *env, float32 src)
{
    return float32_to_int32_round_to_zero(src, &env->fp_status);
}

int32_t helper_fdtoi(CPUSPARCState *env, float64 src)
{
    return float64_to_int32_round_to_zero(src, &env->fp_status);
}

int32_t helper_fqtoi(CPUSPARCState *env)
{
    return float128_to_int32_round_to_zero(QT1, &env->fp_status);
}

#ifdef TARGET_SPARC64
int64_t helper_fstox(CPUSPARCState *env, float32 src)
{
    return float32_to_int64_round_to_zero(src, &env->fp_status);
}

int64_t helper_fdtox(CPUSPARCState *env, float64 src)
{
    return float64_to_int64_round_to_zero(src, &env->fp_status);
}

int64_t helper_fqtox(CPUSPARCState *env)
{
    return float128_to_int64_round_to_zero(QT1, &env->fp_status);
}
#endif

float32 helper_fabss(float32 src)
{
    return float32_abs(src);
}

#ifdef TARGET_SPARC64
float64 helper_fabsd(float64 src)
{
    return float64_abs(src);
}

void helper_fabsq(CPUSPARCState *env)
{
    QT0 = float128_abs(QT1);
}
#endif

float32 helper_fsqrts(CPUSPARCState *env, float32 src)
{
    return float32_sqrt(src, &env->fp_status);
}

float64 helper_fsqrtd(CPUSPARCState *env, float64 src)
{
    return float64_sqrt(src, &env->fp_status);
}

void helper_fsqrtq(CPUSPARCState *env)
{
    QT0 = float128_sqrt(QT1, &env->fp_status);
}

#define GEN_FCMP(name, size, reg1, reg2, FS, E)                         \
    target_ulong glue(helper_, name) (CPUSPARCState *env)               \
    {                                                                   \
        FloatRelation ret;                                              \
        target_ulong fsr;                                               \
        if (E) {                                                        \
            ret = glue(size, _compare)(reg1, reg2, &env->fp_status);    \
        } else {                                                        \
            ret = glue(size, _compare_quiet)(reg1, reg2,                \
                                             &env->fp_status);          \
        }                                                               \
        fsr = do_check_ieee_exceptions(env, GETPC());                   \
        switch (ret) {                                                  \
        case float_relation_unordered:                                  \
            fsr |= (FSR_FCC1 | FSR_FCC0) << FS;                         \
            fsr |= FSR_NVA;                                             \
            break;                                                      \
        case float_relation_less:                                       \
            fsr &= ~(FSR_FCC1) << FS;                                   \
            fsr |= FSR_FCC0 << FS;                                      \
            break;                                                      \
        case float_relation_greater:                                    \
            fsr &= ~(FSR_FCC0) << FS;                                   \
            fsr |= FSR_FCC1 << FS;                                      \
            break;                                                      \
        default:                                                        \
            fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                      \
            break;                                                      \
        }                                                               \
        return fsr;                                                     \
    }
#define GEN_FCMP_T(name, size, FS, E)                                   \
    target_ulong glue(helper_, name)(CPUSPARCState *env, size src1, size src2)\
    {                                                                   \
        FloatRelation ret;                                              \
        target_ulong fsr;                                               \
        if (E) {                                                        \
            ret = glue(size, _compare)(src1, src2, &env->fp_status);    \
        } else {                                                        \
            ret = glue(size, _compare_quiet)(src1, src2,                \
                                             &env->fp_status);          \
        }                                                               \
        fsr = do_check_ieee_exceptions(env, GETPC());                   \
        switch (ret) {                                                  \
        case float_relation_unordered:                                  \
            fsr |= (FSR_FCC1 | FSR_FCC0) << FS;                         \
            break;                                                      \
        case float_relation_less:                                       \
            fsr &= ~(FSR_FCC1 << FS);                                   \
            fsr |= FSR_FCC0 << FS;                                      \
            break;                                                      \
        case float_relation_greater:                                    \
            fsr &= ~(FSR_FCC0 << FS);                                   \
            fsr |= FSR_FCC1 << FS;                                      \
            break;                                                      \
        default:                                                        \
            fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                      \
            break;                                                      \
        }                                                               \
        return fsr;                                                     \
    }

GEN_FCMP_T(fcmps, float32, 0, 0);
GEN_FCMP_T(fcmpd, float64, 0, 0);

GEN_FCMP_T(fcmpes, float32, 0, 1);
GEN_FCMP_T(fcmped, float64, 0, 1);

GEN_FCMP(fcmpq, float128, QT0, QT1, 0, 0);
GEN_FCMP(fcmpeq, float128, QT0, QT1, 0, 1);

#ifdef TARGET_SPARC64
GEN_FCMP_T(fcmps_fcc1, float32, 22, 0);
GEN_FCMP_T(fcmpd_fcc1, float64, 22, 0);
GEN_FCMP(fcmpq_fcc1, float128, QT0, QT1, 22, 0);

GEN_FCMP_T(fcmps_fcc2, float32, 24, 0);
GEN_FCMP_T(fcmpd_fcc2, float64, 24, 0);
GEN_FCMP(fcmpq_fcc2, float128, QT0, QT1, 24, 0);

GEN_FCMP_T(fcmps_fcc3, float32, 26, 0);
GEN_FCMP_T(fcmpd_fcc3, float64, 26, 0);
GEN_FCMP(fcmpq_fcc3, float128, QT0, QT1, 26, 0);

GEN_FCMP_T(fcmpes_fcc1, float32, 22, 1);
GEN_FCMP_T(fcmped_fcc1, float64, 22, 1);
GEN_FCMP(fcmpeq_fcc1, float128, QT0, QT1, 22, 1);

GEN_FCMP_T(fcmpes_fcc2, float32, 24, 1);
GEN_FCMP_T(fcmped_fcc2, float64, 24, 1);
GEN_FCMP(fcmpeq_fcc2, float128, QT0, QT1, 24, 1);

GEN_FCMP_T(fcmpes_fcc3, float32, 26, 1);
GEN_FCMP_T(fcmped_fcc3, float64, 26, 1);
GEN_FCMP(fcmpeq_fcc3, float128, QT0, QT1, 26, 1);
#endif
#undef GEN_FCMP_T
#undef GEN_FCMP

static void set_fsr(CPUSPARCState *env, target_ulong fsr)
{
    int rnd_mode;

    switch (fsr & FSR_RD_MASK) {
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

target_ulong helper_ldfsr(CPUSPARCState *env, target_ulong old_fsr,
                          uint32_t new_fsr)
{
    old_fsr = (new_fsr & FSR_LDFSR_MASK) | (old_fsr & FSR_LDFSR_OLDMASK);
    set_fsr(env, old_fsr);
    return old_fsr;
}

#ifdef TARGET_SPARC64
target_ulong helper_ldxfsr(CPUSPARCState *env, target_ulong old_fsr,
                           uint64_t new_fsr)
{
    old_fsr = (new_fsr & FSR_LDXFSR_MASK) | (old_fsr & FSR_LDXFSR_OLDMASK);
    set_fsr(env, old_fsr);
    return old_fsr;
}
#endif
