#include <stdio.h>

/* float class */
#define FLOAT_CLASS_SIGNALING_NAN      0x001
#define FLOAT_CLASS_QUIET_NAN          0x002
#define FLOAT_CLASS_NEGATIVE_INFINITY  0x004
#define FLOAT_CLASS_NEGATIVE_NORMAL    0x008
#define FLOAT_CLASS_NEGATIVE_SUBNORMAL 0x010
#define FLOAT_CLASS_NEGATIVE_ZERO      0x020
#define FLOAT_CLASS_POSITIVE_INFINITY  0x040
#define FLOAT_CLASS_POSITIVE_NORMAL    0x080
#define FLOAT_CLASS_POSITIVE_SUBNORMAL 0x100
#define FLOAT_CLASS_POSITIVE_ZERO      0x200

#define TEST_FCLASS(N)                            \
void test_fclass_##N(long s)                      \
{                                                 \
    double fd;                                    \
    long rd;                                      \
                                                  \
    asm volatile("fclass."#N" %0, %2\n\t"         \
                 "movfr2gr."#N" %1, %2\n\t"       \
                    : "=f"(fd), "=r"(rd)          \
                    : "f"(s)                      \
                    : );                          \
    switch (rd) {                                 \
    case FLOAT_CLASS_SIGNALING_NAN:               \
    case FLOAT_CLASS_QUIET_NAN:                   \
    case FLOAT_CLASS_NEGATIVE_INFINITY:           \
    case FLOAT_CLASS_NEGATIVE_NORMAL:             \
    case FLOAT_CLASS_NEGATIVE_SUBNORMAL:          \
    case FLOAT_CLASS_NEGATIVE_ZERO:               \
    case FLOAT_CLASS_POSITIVE_INFINITY:           \
    case FLOAT_CLASS_POSITIVE_NORMAL:             \
    case FLOAT_CLASS_POSITIVE_SUBNORMAL:          \
    case FLOAT_CLASS_POSITIVE_ZERO:               \
        break;                                    \
    default:                                      \
        printf("fclass."#N" test failed.\n");     \
        break;                                    \
    }                                             \
}

/*
 *  float format
 *  type     |    S  | Exponent  |  Fraction    |  example value
 *                31 | 30 --23   | 22  | 21 --0 |
 *                               | bit |
 *  SNAN         0/1 |   0xFF    | 0   |  !=0   |  0x7FBFFFFF
 *  QNAN         0/1 |   0xFF    | 1   |        |  0x7FCFFFFF
 *  -infinity     1  |   0xFF    |     0        |  0xFF800000
 *  -normal       1  | [1, 0xFE] | [0, 0x7FFFFF]|  0xFF7FFFFF
 *  -subnormal    1  |    0      |    !=0       |  0x807FFFFF
 *  -0            1  |    0      |     0        |  0x80000000
 *  +infinity     0  |   0xFF    |     0        |  0x7F800000
 *  +normal       0  | [1, 0xFE] | [0, 0x7FFFFF]|  0x7F7FFFFF
 *  +subnormal    0  |    0      |    !=0       |  0x007FFFFF
 *  +0            0  |    0      |     0        |  0x00000000
 */

long float_snan = 0x7FBFFFFF;
long float_qnan = 0x7FCFFFFF;
long float_neg_infinity = 0xFF800000;
long float_neg_normal = 0xFF7FFFFF;
long float_neg_subnormal = 0x807FFFFF;
long float_neg_zero = 0x80000000;
long float_post_infinity = 0x7F800000;
long float_post_normal = 0x7F7FFFFF;
long float_post_subnormal = 0x007FFFFF;
long float_post_zero = 0x00000000;

/*
 * double format
 *  type     |    S  | Exponent  |  Fraction     |  example value
 *                63 | 62  -- 52 | 51  | 50 -- 0 |
 *                               | bit |
 *  SNAN         0/1 |  0x7FF    | 0   |  !=0    | 0x7FF7FFFFFFFFFFFF
 *  QNAN         0/1 |  0x7FF    | 1   |         | 0x7FFFFFFFFFFFFFFF
 * -infinity      1  |  0x7FF    |    0          | 0xFFF0000000000000
 * -normal        1  |[1, 0x7FE] |               | 0xFFEFFFFFFFFFFFFF
 * -subnormal     1  |   0       |   !=0         | 0x8007FFFFFFFFFFFF
 * -0             1  |   0       |    0          | 0x8000000000000000
 * +infinity      0  |  0x7FF    |    0          | 0x7FF0000000000000
 * +normal        0  |[1, 0x7FE] |               | 0x7FEFFFFFFFFFFFFF
 * +subnormal     0  |  0        |   !=0         | 0x000FFFFFFFFFFFFF
 * +0             0  |  0        |   0           | 0x0000000000000000
 */

long double_snan = 0x7FF7FFFFFFFFFFFF;
long double_qnan = 0x7FFFFFFFFFFFFFFF;
long double_neg_infinity = 0xFFF0000000000000;
long double_neg_normal = 0xFFEFFFFFFFFFFFFF;
long double_neg_subnormal = 0x8007FFFFFFFFFFFF;
long double_neg_zero = 0x8000000000000000;
long double_post_infinity = 0x7FF0000000000000;
long double_post_normal = 0x7FEFFFFFFFFFFFFF;
long double_post_subnormal = 0x000FFFFFFFFFFFFF;
long double_post_zero = 0x0000000000000000;

TEST_FCLASS(s)
TEST_FCLASS(d)

int main()
{
    /* fclass.s */
    test_fclass_s(float_snan);
    test_fclass_s(float_qnan);
    test_fclass_s(float_neg_infinity);
    test_fclass_s(float_neg_normal);
    test_fclass_s(float_neg_subnormal);
    test_fclass_s(float_neg_zero);
    test_fclass_s(float_post_infinity);
    test_fclass_s(float_post_normal);
    test_fclass_s(float_post_subnormal);
    test_fclass_s(float_post_zero);

    /* fclass.d */
    test_fclass_d(double_snan);
    test_fclass_d(double_qnan);
    test_fclass_d(double_neg_infinity);
    test_fclass_d(double_neg_normal);
    test_fclass_d(double_neg_subnormal);
    test_fclass_d(double_neg_zero);
    test_fclass_d(double_post_infinity);
    test_fclass_d(double_post_normal);
    test_fclass_d(double_post_subnormal);
    test_fclass_d(double_post_zero);

    return 0;
}
