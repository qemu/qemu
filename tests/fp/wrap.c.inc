/*
 * In this file we wrap QEMU FP functions to look like softfloat/testfloat's,
 * so that we can use the testfloat infrastructure as-is.
 *
 * This file must be included directly from fp-test.c. We could compile it
 * separately, but it would be tedious to add declarations for all the wrappers.
 */

static signed char sf_tininess_to_qemu(uint_fast8_t mode)
{
    switch (mode) {
    case softfloat_tininess_beforeRounding:
        return float_tininess_before_rounding;
    case softfloat_tininess_afterRounding:
        return float_tininess_after_rounding;
    default:
        g_assert_not_reached();
    }
}

static signed char sf_rounding_to_qemu(uint_fast8_t mode)
{
    switch (mode) {
    case softfloat_round_near_even:
        return float_round_nearest_even;
    case softfloat_round_minMag:
        return float_round_to_zero;
    case softfloat_round_min:
        return float_round_down;
    case softfloat_round_max:
        return float_round_up;
    case softfloat_round_near_maxMag:
        return float_round_ties_away;
    case softfloat_round_odd:
        return float_round_to_odd;
    default:
        g_assert_not_reached();
    }
}

static uint_fast8_t qemu_flags_to_sf(uint8_t qflags)
{
    uint_fast8_t ret = 0;

    if (qflags & float_flag_invalid) {
        ret |= softfloat_flag_invalid;
    }
    if (qflags & float_flag_divbyzero) {
        ret |= softfloat_flag_infinite;
    }
    if (qflags & float_flag_overflow) {
        ret |= softfloat_flag_overflow;
    }
    if (qflags & float_flag_underflow) {
        ret |= softfloat_flag_underflow;
    }
    if (qflags & float_flag_inexact) {
        ret |= softfloat_flag_inexact;
    }
    return ret;
}

/*
 * floatx80 and float128 cannot be cast between qemu and softfloat, because
 * in softfloat the order of the fields depends on the host's endianness.
 */
static extFloat80_t qemu_to_soft80(floatx80 a)
{
    extFloat80_t ret;

    ret.signif = a.low;
    ret.signExp = a.high;
    return ret;
}

static floatx80 soft_to_qemu80(extFloat80_t a)
{
    floatx80 ret;

    ret.low = a.signif;
    ret.high = a.signExp;
    return ret;
}

static float128_t qemu_to_soft128(float128 a)
{
    float128_t ret;
    struct uint128 *to = (struct uint128 *)&ret;

    to->v0 = a.low;
    to->v64 = a.high;
    return ret;
}

static float128 soft_to_qemu128(float128_t a)
{
    struct uint128 *from = (struct uint128 *)&a;
    float128 ret;

    ret.low = from->v0;
    ret.high = from->v64;
    return ret;
}

/* conversions */
#define WRAP_SF_TO_SF_IEEE(name, func, a_type, b_type)  \
    static b_type##_t name(a_type##_t a)                \
    {                                                   \
        a_type *ap = (a_type *)&a;                      \
        b_type ret;                                     \
                                                        \
        ret = func(*ap, true, &qsf);                    \
        return *(b_type##_t *)&ret;                     \
    }

WRAP_SF_TO_SF_IEEE(qemu_f16_to_f32, float16_to_float32, float16, float32)
WRAP_SF_TO_SF_IEEE(qemu_f16_to_f64, float16_to_float64, float16, float64)

WRAP_SF_TO_SF_IEEE(qemu_f32_to_f16, float32_to_float16, float32, float16)
WRAP_SF_TO_SF_IEEE(qemu_f64_to_f16, float64_to_float16, float64, float16)
#undef WRAP_SF_TO_SF_IEEE

#define WRAP_SF_TO_SF(name, func, a_type, b_type)       \
    static b_type##_t name(a_type##_t a)                \
    {                                                   \
        a_type *ap = (a_type *)&a;                      \
        b_type ret;                                     \
                                                        \
        ret = func(*ap, &qsf);                          \
        return *(b_type##_t *)&ret;                     \
    }

WRAP_SF_TO_SF(qemu_f32_to_f64, float32_to_float64, float32, float64)
WRAP_SF_TO_SF(qemu_f64_to_f32, float64_to_float32, float64, float32)
#undef WRAP_SF_TO_SF

#define WRAP_SF_TO_80(name, func, type)                 \
    static void name(type##_t a, extFloat80_t *res)     \
    {                                                   \
        floatx80 ret;                                   \
        type *ap = (type *)&a;                          \
                                                        \
        ret = func(*ap, &qsf);                          \
        *res = qemu_to_soft80(ret);                     \
    }

WRAP_SF_TO_80(qemu_f32_to_extF80M, float32_to_floatx80, float32)
WRAP_SF_TO_80(qemu_f64_to_extF80M, float64_to_floatx80, float64)
#undef WRAP_SF_TO_80

#define WRAP_SF_TO_128(name, func, type)                \
    static void name(type##_t a, float128_t *res)       \
    {                                                   \
        float128 ret;                                   \
        type *ap = (type *)&a;                          \
                                                        \
        ret = func(*ap, &qsf);                          \
        *res = qemu_to_soft128(ret);                    \
    }

WRAP_SF_TO_128(qemu_f32_to_f128M, float32_to_float128, float32)
WRAP_SF_TO_128(qemu_f64_to_f128M, float64_to_float128, float64)
#undef WRAP_SF_TO_128

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_SF_TO_INT(name, func, type, fast_type)                     \
    static fast_type name(type##_t a, uint_fast8_t round, bool exact)   \
    {                                                                   \
        type *ap = (type *)&a;                                          \
                                                                        \
        qsf.float_rounding_mode = sf_rounding_to_qemu(round);           \
        return func(*ap, &qsf);                                         \
    }

WRAP_SF_TO_INT(qemu_f16_to_ui32, float16_to_uint32, float16, uint_fast32_t)
WRAP_SF_TO_INT(qemu_f16_to_ui64, float16_to_uint64, float16, uint_fast64_t)

WRAP_SF_TO_INT(qemu_f32_to_ui32, float32_to_uint32, float32, uint_fast32_t)
WRAP_SF_TO_INT(qemu_f32_to_ui64, float32_to_uint64, float32, uint_fast64_t)

WRAP_SF_TO_INT(qemu_f64_to_ui32, float64_to_uint32, float64, uint_fast32_t)
WRAP_SF_TO_INT(qemu_f64_to_ui64, float64_to_uint64, float64, uint_fast64_t)

WRAP_SF_TO_INT(qemu_f16_to_i32, float16_to_int32, float16, int_fast32_t)
WRAP_SF_TO_INT(qemu_f16_to_i64, float16_to_int64, float16, int_fast64_t)

WRAP_SF_TO_INT(qemu_f32_to_i32, float32_to_int32, float32, int_fast32_t)
WRAP_SF_TO_INT(qemu_f32_to_i64, float32_to_int64, float32, int_fast64_t)

WRAP_SF_TO_INT(qemu_f64_to_i32, float64_to_int32, float64, int_fast32_t)
WRAP_SF_TO_INT(qemu_f64_to_i64, float64_to_int64, float64, int_fast64_t)
#undef WRAP_SF_TO_INT

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_SF_TO_INT_MINMAG(name, func, type, fast_type)      \
    static fast_type name(type##_t a, bool exact)               \
    {                                                           \
        type *ap = (type *)&a;                                  \
                                                                \
        return func(*ap, &qsf);                                 \
    }

WRAP_SF_TO_INT_MINMAG(qemu_f16_to_ui32_r_minMag,
                      float16_to_uint32_round_to_zero, float16, uint_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f16_to_ui64_r_minMag,
                      float16_to_uint64_round_to_zero, float16, uint_fast64_t)

WRAP_SF_TO_INT_MINMAG(qemu_f16_to_i32_r_minMag,
                      float16_to_int32_round_to_zero, float16, int_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f16_to_i64_r_minMag,
                      float16_to_int64_round_to_zero, float16, int_fast64_t)

WRAP_SF_TO_INT_MINMAG(qemu_f32_to_ui32_r_minMag,
                      float32_to_uint32_round_to_zero, float32, uint_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f32_to_ui64_r_minMag,
                      float32_to_uint64_round_to_zero, float32, uint_fast64_t)

WRAP_SF_TO_INT_MINMAG(qemu_f32_to_i32_r_minMag,
                      float32_to_int32_round_to_zero, float32, int_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f32_to_i64_r_minMag,
                      float32_to_int64_round_to_zero, float32, int_fast64_t)

WRAP_SF_TO_INT_MINMAG(qemu_f64_to_ui32_r_minMag,
                      float64_to_uint32_round_to_zero, float64, uint_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f64_to_ui64_r_minMag,
                      float64_to_uint64_round_to_zero, float64, uint_fast64_t)

WRAP_SF_TO_INT_MINMAG(qemu_f64_to_i32_r_minMag,
                      float64_to_int32_round_to_zero, float64, int_fast32_t)
WRAP_SF_TO_INT_MINMAG(qemu_f64_to_i64_r_minMag,
                      float64_to_int64_round_to_zero, float64, int_fast64_t)
#undef WRAP_SF_TO_INT_MINMAG

#define WRAP_80_TO_SF(name, func, type)                 \
    static type##_t name(const extFloat80_t *ap)        \
    {                                                   \
        floatx80 a;                                     \
        type ret;                                       \
                                                        \
        a = soft_to_qemu80(*ap);                        \
        ret = func(a, &qsf);                            \
        return *(type##_t *)&ret;                       \
    }

WRAP_80_TO_SF(qemu_extF80M_to_f32, floatx80_to_float32, float32)
WRAP_80_TO_SF(qemu_extF80M_to_f64, floatx80_to_float64, float64)
#undef WRAP_80_TO_SF

#define WRAP_128_TO_SF(name, func, type)        \
    static type##_t name(const float128_t *ap)  \
    {                                           \
        float128 a;                             \
        type ret;                               \
                                                \
        a = soft_to_qemu128(*ap);               \
        ret = func(a, &qsf);                    \
        return *(type##_t *)&ret;               \
    }

WRAP_128_TO_SF(qemu_f128M_to_f32, float128_to_float32, float32)
WRAP_128_TO_SF(qemu_f128M_to_f64, float128_to_float64, float64)
#undef WRAP_128_TO_SF

static void qemu_extF80M_to_f128M(const extFloat80_t *from, float128_t *to)
{
    floatx80 qfrom;
    float128 qto;

    qfrom = soft_to_qemu80(*from);
    qto = floatx80_to_float128(qfrom, &qsf);
    *to = qemu_to_soft128(qto);
}

static void qemu_f128M_to_extF80M(const float128_t *from, extFloat80_t *to)
{
    float128 qfrom;
    floatx80 qto;

    qfrom = soft_to_qemu128(*from);
    qto = float128_to_floatx80(qfrom, &qsf);
    *to = qemu_to_soft80(qto);
}

#define WRAP_INT_TO_SF(name, func, int_type, type)      \
    static type##_t name(int_type a)                    \
    {                                                   \
        type ret;                                       \
                                                        \
        ret = func(a, &qsf);                            \
        return *(type##_t *)&ret;                       \
    }

WRAP_INT_TO_SF(qemu_ui32_to_f16, uint32_to_float16, uint32_t, float16)
WRAP_INT_TO_SF(qemu_ui32_to_f32, uint32_to_float32, uint32_t, float32)
WRAP_INT_TO_SF(qemu_ui32_to_f64, uint32_to_float64, uint32_t, float64)

WRAP_INT_TO_SF(qemu_ui64_to_f16, uint64_to_float16, uint64_t, float16)
WRAP_INT_TO_SF(qemu_ui64_to_f32, uint64_to_float32, uint64_t, float32)
WRAP_INT_TO_SF(qemu_ui64_to_f64, uint64_to_float64, uint64_t, float64)

WRAP_INT_TO_SF(qemu_i32_to_f16, int32_to_float16, int32_t, float16)
WRAP_INT_TO_SF(qemu_i32_to_f32, int32_to_float32, int32_t, float32)
WRAP_INT_TO_SF(qemu_i32_to_f64, int32_to_float64, int32_t, float64)

WRAP_INT_TO_SF(qemu_i64_to_f16, int64_to_float16, int64_t, float16)
WRAP_INT_TO_SF(qemu_i64_to_f32, int64_to_float32, int64_t, float32)
WRAP_INT_TO_SF(qemu_i64_to_f64, int64_to_float64, int64_t, float64)
#undef WRAP_INT_TO_SF

#define WRAP_INT_TO_80(name, func, int_type)            \
    static void name(int_type a, extFloat80_t *res)     \
    {                                                   \
        floatx80 ret;                                   \
                                                        \
        ret = func(a, &qsf);                            \
        *res = qemu_to_soft80(ret);                     \
    }

WRAP_INT_TO_80(qemu_i32_to_extF80M, int32_to_floatx80, int32_t)
WRAP_INT_TO_80(qemu_i64_to_extF80M, int64_to_floatx80, int64_t)
#undef WRAP_INT_TO_80

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_80_TO_INT(name, func, fast_type)                           \
    static fast_type name(const extFloat80_t *ap, uint_fast8_t round,   \
                          bool exact)                                   \
    {                                                                   \
        floatx80 a;                                                     \
                                                                        \
        a = soft_to_qemu80(*ap);                                        \
        qsf.float_rounding_mode = sf_rounding_to_qemu(round);           \
        return func(a, &qsf);                                           \
    }

WRAP_80_TO_INT(qemu_extF80M_to_i32, floatx80_to_int32, int_fast32_t)
WRAP_80_TO_INT(qemu_extF80M_to_i64, floatx80_to_int64, int_fast64_t)
#undef WRAP_80_TO_INT

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_80_TO_INT_MINMAG(name, func, fast_type)            \
    static fast_type name(const extFloat80_t *ap, bool exact)   \
    {                                                           \
        floatx80 a;                                             \
                                                                \
        a = soft_to_qemu80(*ap);                                \
        return func(a, &qsf);                                   \
    }

WRAP_80_TO_INT_MINMAG(qemu_extF80M_to_i32_r_minMag,
                      floatx80_to_int32_round_to_zero, int_fast32_t)
WRAP_80_TO_INT_MINMAG(qemu_extF80M_to_i64_r_minMag,
                      floatx80_to_int64_round_to_zero, int_fast64_t)
#undef WRAP_80_TO_INT_MINMAG

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_128_TO_INT(name, func, fast_type)                          \
    static fast_type name(const float128_t *ap, uint_fast8_t round,     \
                          bool exact)                                   \
    {                                                                   \
        float128 a;                                                     \
                                                                        \
        a = soft_to_qemu128(*ap);                                       \
        qsf.float_rounding_mode = sf_rounding_to_qemu(round);           \
        return func(a, &qsf);                                           \
    }

WRAP_128_TO_INT(qemu_f128M_to_i32, float128_to_int32, int_fast32_t)
WRAP_128_TO_INT(qemu_f128M_to_i64, float128_to_int64, int_fast64_t)

WRAP_128_TO_INT(qemu_f128M_to_ui32, float128_to_uint32, uint_fast32_t)
WRAP_128_TO_INT(qemu_f128M_to_ui64, float128_to_uint64, uint_fast64_t)
#undef WRAP_128_TO_INT

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_128_TO_INT_MINMAG(name, func, fast_type)           \
    static fast_type name(const float128_t *ap, bool exact)     \
    {                                                           \
        float128 a;                                             \
                                                                \
        a = soft_to_qemu128(*ap);                               \
        return func(a, &qsf);                                   \
    }

WRAP_128_TO_INT_MINMAG(qemu_f128M_to_i32_r_minMag,
                       float128_to_int32_round_to_zero, int_fast32_t)
WRAP_128_TO_INT_MINMAG(qemu_f128M_to_i64_r_minMag,
                       float128_to_int64_round_to_zero, int_fast64_t)

WRAP_128_TO_INT_MINMAG(qemu_f128M_to_ui32_r_minMag,
                       float128_to_uint32_round_to_zero, uint_fast32_t)
WRAP_128_TO_INT_MINMAG(qemu_f128M_to_ui64_r_minMag,
                       float128_to_uint64_round_to_zero, uint_fast64_t)
#undef WRAP_128_TO_INT_MINMAG

#define WRAP_INT_TO_128(name, func, int_type)           \
    static void name(int_type a, float128_t *res)       \
    {                                                   \
        float128 ret;                                   \
                                                        \
        ret = func(a, &qsf);                            \
        *res = qemu_to_soft128(ret);                    \
    }

WRAP_INT_TO_128(qemu_ui64_to_f128M, uint64_to_float128, uint64_t)

WRAP_INT_TO_128(qemu_i32_to_f128M, int32_to_float128, int32_t)
WRAP_INT_TO_128(qemu_i64_to_f128M, int64_to_float128, int64_t)
#undef WRAP_INT_TO_128

/* Note: exact is ignored since qemu's softfloat assumes it is set */
#define WRAP_ROUND_TO_INT(name, func, type)                             \
    static type##_t name(type##_t a, uint_fast8_t round, bool exact)    \
    {                                                                   \
        type *ap = (type *)&a;                                          \
        type ret;                                                       \
                                                                        \
        qsf.float_rounding_mode = sf_rounding_to_qemu(round);           \
        ret = func(*ap, &qsf);                                          \
        return *(type##_t *)&ret;                                       \
    }

WRAP_ROUND_TO_INT(qemu_f16_roundToInt, float16_round_to_int, float16)
WRAP_ROUND_TO_INT(qemu_f32_roundToInt, float32_round_to_int, float32)
WRAP_ROUND_TO_INT(qemu_f64_roundToInt, float64_round_to_int, float64)
#undef WRAP_ROUND_TO_INT

static void qemu_extF80M_roundToInt(const extFloat80_t *ap, uint_fast8_t round,
                                    bool exact, extFloat80_t *res)
{
    floatx80 a;
    floatx80 ret;

    a = soft_to_qemu80(*ap);
    qsf.float_rounding_mode = sf_rounding_to_qemu(round);
    ret = floatx80_round_to_int(a, &qsf);
    *res = qemu_to_soft80(ret);
}

static void qemu_f128M_roundToInt(const float128_t *ap, uint_fast8_t round,
                                  bool exact, float128_t *res)
{
    float128 a;
    float128 ret;

    a = soft_to_qemu128(*ap);
    qsf.float_rounding_mode = sf_rounding_to_qemu(round);
    ret = float128_round_to_int(a, &qsf);
    *res = qemu_to_soft128(ret);
}

/* operations */
#define WRAP1(name, func, type)                 \
    static type##_t name(type##_t a)            \
    {                                           \
        type *ap = (type *)&a;                  \
        type ret;                               \
                                                \
        ret = func(*ap, &qsf);                  \
        return *(type##_t *)&ret;               \
    }

#define WRAP2(name, func, type)                         \
    static type##_t name(type##_t a, type##_t b)        \
    {                                                   \
        type *ap = (type *)&a;                          \
        type *bp = (type *)&b;                          \
        type ret;                                       \
                                                        \
        ret = func(*ap, *bp, &qsf);                     \
        return *(type##_t *)&ret;                       \
    }

#define WRAP_COMMON_OPS(b)                              \
    WRAP1(qemu_f##b##_sqrt, float##b##_sqrt, float##b)  \
    WRAP2(qemu_f##b##_add, float##b##_add, float##b)    \
    WRAP2(qemu_f##b##_sub, float##b##_sub, float##b)    \
    WRAP2(qemu_f##b##_mul, float##b##_mul, float##b)    \
    WRAP2(qemu_f##b##_div, float##b##_div, float##b)

WRAP_COMMON_OPS(16)
WRAP_COMMON_OPS(32)
WRAP_COMMON_OPS(64)
#undef WRAP_COMMON

WRAP2(qemu_f32_rem, float32_rem, float32)
WRAP2(qemu_f64_rem, float64_rem, float64)
#undef WRAP2
#undef WRAP1

#define WRAP1_80(name, func)                                    \
    static void name(const extFloat80_t *ap, extFloat80_t *res) \
    {                                                           \
        floatx80 a;                                             \
        floatx80 ret;                                           \
                                                                \
        a = soft_to_qemu80(*ap);                                \
        ret = func(a, &qsf);                                    \
        *res = qemu_to_soft80(ret);                             \
    }

WRAP1_80(qemu_extF80M_sqrt, floatx80_sqrt)
#undef WRAP1_80

#define WRAP1_128(name, func)                                   \
    static void name(const float128_t *ap, float128_t *res)     \
    {                                                           \
        float128 a;                                             \
        float128 ret;                                           \
                                                                \
        a = soft_to_qemu128(*ap);                               \
        ret = func(a, &qsf);                                    \
        *res = qemu_to_soft128(ret);                            \
    }

WRAP1_128(qemu_f128M_sqrt, float128_sqrt)
#undef WRAP1_128

#define WRAP2_80(name, func)                                            \
    static void name(const extFloat80_t *ap, const extFloat80_t *bp,    \
                     extFloat80_t *res)                                 \
    {                                                                   \
        floatx80 a;                                                     \
        floatx80 b;                                                     \
        floatx80 ret;                                                   \
                                                                        \
        a = soft_to_qemu80(*ap);                                        \
        b = soft_to_qemu80(*bp);                                        \
        ret = func(a, b, &qsf);                                         \
        *res = qemu_to_soft80(ret);                                     \
    }

WRAP2_80(qemu_extF80M_add, floatx80_add)
WRAP2_80(qemu_extF80M_sub, floatx80_sub)
WRAP2_80(qemu_extF80M_mul, floatx80_mul)
WRAP2_80(qemu_extF80M_div, floatx80_div)
WRAP2_80(qemu_extF80M_rem, floatx80_rem)
#undef WRAP2_80

#define WRAP2_128(name, func)                                           \
    static void name(const float128_t *ap, const float128_t *bp,        \
                     float128_t *res)                                   \
    {                                                                   \
        float128 a;                                                     \
        float128 b;                                                     \
        float128 ret;                                                   \
                                                                        \
        a = soft_to_qemu128(*ap);                                       \
        b = soft_to_qemu128(*bp);                                       \
        ret = func(a, b, &qsf);                                         \
        *res = qemu_to_soft128(ret);                                    \
    }

WRAP2_128(qemu_f128M_add, float128_add)
WRAP2_128(qemu_f128M_sub, float128_sub)
WRAP2_128(qemu_f128M_mul, float128_mul)
WRAP2_128(qemu_f128M_div, float128_div)
WRAP2_128(qemu_f128M_rem, float128_rem)
#undef WRAP2_128

#define WRAP_MULADD(name, func, type)                           \
    static type##_t name(type##_t a, type##_t b, type##_t c)    \
    {                                                           \
        type *ap = (type *)&a;                                  \
        type *bp = (type *)&b;                                  \
        type *cp = (type *)&c;                                  \
        type ret;                                               \
                                                                \
        ret = func(*ap, *bp, *cp, 0, &qsf);                     \
        return *(type##_t *)&ret;                               \
    }

WRAP_MULADD(qemu_f16_mulAdd, float16_muladd, float16)
WRAP_MULADD(qemu_f32_mulAdd, float32_muladd, float32)
WRAP_MULADD(qemu_f64_mulAdd, float64_muladd, float64)
#undef WRAP_MULADD

static void qemu_f128M_mulAdd(const float128_t *ap, const float128_t *bp,
                              const float128_t *cp, float128_t *res)
{
    float128 a, b, c, ret;

    a = soft_to_qemu128(*ap);
    b = soft_to_qemu128(*bp);
    c = soft_to_qemu128(*cp);
    ret = float128_muladd(a, b, c, 0, &qsf);
    *res = qemu_to_soft128(ret);
}

#define WRAP_CMP16(name, func, retcond)         \
    static bool name(float16_t a, float16_t b)  \
    {                                           \
        float16 *ap = (float16 *)&a;            \
        float16 *bp = (float16 *)&b;            \
        int ret;                                \
                                                \
        ret = func(*ap, *bp, &qsf);             \
        return retcond;                         \
    }

WRAP_CMP16(qemu_f16_eq_signaling, float16_compare, ret == 0)
WRAP_CMP16(qemu_f16_eq, float16_compare_quiet, ret == 0)
WRAP_CMP16(qemu_f16_le, float16_compare, ret <= 0)
WRAP_CMP16(qemu_f16_lt, float16_compare, ret < 0)
WRAP_CMP16(qemu_f16_le_quiet, float16_compare_quiet, ret <= 0)
WRAP_CMP16(qemu_f16_lt_quiet, float16_compare_quiet, ret < 0)
#undef WRAP_CMP16

#define WRAP_CMP(name, func, type)              \
    static bool name(type##_t a, type##_t b)    \
    {                                           \
        type *ap = (type *)&a;                  \
        type *bp = (type *)&b;                  \
                                                \
        return !!func(*ap, *bp, &qsf);          \
    }

#define GEN_WRAP_CMP(b)                                                 \
    WRAP_CMP(qemu_f##b##_eq_signaling, float##b##_eq, float##b)         \
    WRAP_CMP(qemu_f##b##_eq, float##b##_eq_quiet, float##b)             \
    WRAP_CMP(qemu_f##b##_le, float##b##_le, float##b)                   \
    WRAP_CMP(qemu_f##b##_lt, float##b##_lt, float##b)                   \
    WRAP_CMP(qemu_f##b##_le_quiet, float##b##_le_quiet, float##b)       \
    WRAP_CMP(qemu_f##b##_lt_quiet, float##b##_lt_quiet, float##b)

GEN_WRAP_CMP(32)
GEN_WRAP_CMP(64)
#undef GEN_WRAP_CMP
#undef WRAP_CMP

#define WRAP_CMP80(name, func)                                          \
    static bool name(const extFloat80_t *ap, const extFloat80_t *bp)    \
    {                                                                   \
        floatx80 a;                                                     \
        floatx80 b;                                                     \
                                                                        \
        a = soft_to_qemu80(*ap);                                        \
        b = soft_to_qemu80(*bp);                                        \
        return !!func(a, b, &qsf);                                      \
    }

WRAP_CMP80(qemu_extF80M_eq_signaling, floatx80_eq)
WRAP_CMP80(qemu_extF80M_eq, floatx80_eq_quiet)
WRAP_CMP80(qemu_extF80M_le, floatx80_le)
WRAP_CMP80(qemu_extF80M_lt, floatx80_lt)
WRAP_CMP80(qemu_extF80M_le_quiet, floatx80_le_quiet)
WRAP_CMP80(qemu_extF80M_lt_quiet, floatx80_lt_quiet)
#undef WRAP_CMP80

#define WRAP_CMP128(name, func)                                         \
    static bool name(const float128_t *ap, const float128_t *bp)        \
    {                                                                   \
        float128 a;                                                     \
        float128 b;                                                     \
                                                                        \
        a = soft_to_qemu128(*ap);                                       \
        b = soft_to_qemu128(*bp);                                       \
        return !!func(a, b, &qsf);                                      \
    }

WRAP_CMP128(qemu_f128M_eq_signaling, float128_eq)
WRAP_CMP128(qemu_f128M_eq, float128_eq_quiet)
WRAP_CMP128(qemu_f128M_le, float128_le)
WRAP_CMP128(qemu_f128M_lt, float128_lt)
WRAP_CMP128(qemu_f128M_le_quiet, float128_le_quiet)
WRAP_CMP128(qemu_f128M_lt_quiet, float128_lt_quiet)
#undef WRAP_CMP128
