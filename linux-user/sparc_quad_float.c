#include "exec.h"
#include "host-utils.h"
#include "helper.h"

#define F_HELPER(name, p) void helper_f##name##p(void)

#define F_BINOP(name)                                           \
    F_HELPER(name, q)                                           \
    {                                                           \
        QT0 = float128_ ## name (QT0, QT1, &env->fp_status);    \
    }

F_BINOP(add);
F_BINOP(sub);
F_BINOP(mul);
F_BINOP(div);
#undef F_BINOP

void helper_fdmulq(void)
{
    QT0 = float128_mul(float64_to_float128(DT0, &env->fp_status),
                       float64_to_float128(DT1, &env->fp_status),
                       &env->fp_status);
}

F_HELPER(ito, q)
{
    QT0 = int32_to_float128(*((int32_t *)&FT1), &env->fp_status);
}

void helper_fqtos(void)
{
    FT0 = float128_to_float32(QT1, &env->fp_status);
}

void helper_fstoq(void)
{
    QT0 = float32_to_float128(FT1, &env->fp_status);
}

void helper_fqtod(void)
{
    DT0 = float128_to_float64(QT1, &env->fp_status);
}

void helper_fdtoq(void)
{
    QT0 = float64_to_float128(DT1, &env->fp_status);
}

void helper_fqtoi(void)
{
    *((int32_t *)&FT0) = float128_to_int32_round_to_zero(QT1, &env->fp_status);
}

void helper_fsqrtq(void)
{
    QT0 = float128_sqrt(QT1, &env->fp_status);
}

#ifdef TARGET_SPARC64
F_HELPER(neg, q)
{
    QT0 = float128_chs(QT1);
}

F_HELPER(xto, q)
{
    QT0 = int64_to_float128(*((int64_t *)&DT1), &env->fp_status);
}

void helper_fqtox(void)
{
    *((int64_t *)&DT0) = float128_to_int64_round_to_zero(QT1, &env->fp_status);
}

void helper_fabsq(void)
{
    QT0 = float128_abs(QT1);
}
#endif

#define GEN_FCMP(name, size, reg1, reg2, FS, TRAP)                      \
    void glue(helper_, name) (void)                                     \
    {                                                                   \
        target_ulong new_fsr;                                           \
                                                                        \
        env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                     \
        switch (glue(size, _compare) (reg1, reg2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            new_fsr = (FSR_FCC1 | FSR_FCC0) << FS;                      \
            if ((env->fsr & FSR_NVM) || TRAP) {                         \
                env->fsr |= new_fsr;                                    \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                raise_exception(TT_FP_EXCP);                            \
            } else {                                                    \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            new_fsr = FSR_FCC0 << FS;                                   \
            break;                                                      \
        case float_relation_greater:                                    \
            new_fsr = FSR_FCC1 << FS;                                   \
            break;                                                      \
        default:                                                        \
            new_fsr = 0;                                                \
            break;                                                      \
        }                                                               \
        env->fsr |= new_fsr;                                            \
    }

GEN_FCMP(fcmpq, float128, QT0, QT1, 0, 0);
GEN_FCMP(fcmpeq, float128, QT0, QT1, 0, 1);

#ifdef TARGET_SPARC64
GEN_FCMP(fcmpq_fcc1, float128, QT0, QT1, 22, 0);
GEN_FCMP(fcmpq_fcc2, float128, QT0, QT1, 24, 0);
GEN_FCMP(fcmpq_fcc3, float128, QT0, QT1, 26, 0);
GEN_FCMP(fcmpeq_fcc1, float128, QT0, QT1, 22, 1);
GEN_FCMP(fcmpeq_fcc2, float128, QT0, QT1, 24, 1);
GEN_FCMP(fcmpeq_fcc3, float128, QT0, QT1, 26, 1);
#endif
