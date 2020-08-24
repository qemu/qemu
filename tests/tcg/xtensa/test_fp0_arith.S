#include "macros.inc"
#include "fpu.h"

test_suite fp0_arith

#if XCHAL_HAVE_FP

.macro movfp fr, v
    movi    a2, \v
    wfr     \fr, a2
.endm

.macro check_res fr, r, sr
    rfr     a2, \fr
    dump    a2
    movi    a3, \r
    assert  eq, a2, a3
    rur     a2, fsr
#if DFPU
    movi    a3, \sr
    assert  eq, a2, a3
#else
    assert  eqi, a2, 0
#endif
.endm

test add_s
    movi    a2, 1
    wsr     a2, cpenable

    test_op2 add.s, f0, f1, f2, 0x3fc00000, 0x34400000, \
        0x3fc00002, 0x3fc00001, 0x3fc00002, 0x3fc00001, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
    test_op2 add.s, f3, f4, f5, 0x3fc00000, 0x34a00000, \
        0x3fc00002, 0x3fc00002, 0x3fc00003, 0x3fc00002, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I

    /* MAX_FLOAT + MAX_FLOAT = +inf/MAX_FLOAT  */
    test_op2 add.s, f6, f7, f8, 0x7f7fffff, 0x7f7fffff, \
        0x7f800000, 0x7f7fffff, 0x7f800000, 0x7f7fffff, \
            FSR_OI,     FSR_OI,     FSR_OI,     FSR_OI
test_end

test add_s_inf
    /* 1 + +inf = +inf  */
    test_op2 add.s, f6, f7, f8, 0x3fc00000, 0x7f800000, \
        0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000, \
             FSR__,      FSR__,      FSR__,      FSR__

    /* +inf + -inf = default NaN */
    test_op2 add.s, f0, f1, f2, 0x7f800000, 0xff800000, \
        0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
test_end

#if DFPU
test add_s_nan_dfpu
    /* 1 + QNaN = QNaN  */
    test_op2 add.s, f9, f10, f11, 0x3fc00000, 0x7fc00001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR__,      FSR__,      FSR__,      FSR__
    /* 1 + SNaN = QNaN  */
    test_op2 add.s, f12, f13, f14, 0x3fc00000, 0x7f800001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V

    /* SNaN1 + SNaN2 = QNaN2 */
    test_op2 add.s, f15, f0, f1, 0x7f800001, 0x7fbfffff, \
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
    test_op2 add.s, f2, f3, f4, 0x7fbfffff, 0x7f800001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
    /* QNaN1 + SNaN2 = QNaN2 */
    test_op2 add.s, f5, f6, f7, 0x7fc00001, 0x7fbfffff, \
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
    /* SNaN1 + QNaN2 = QNaN2 */
    test_op2 add.s, f8, f9, f10, 0x7fbfffff, 0x7fc00001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
test_end
#else
test add_s_nan_fpu2k
    /* 1 + QNaN = QNaN  */
    test_op2 add.s, f9, f10, f11, 0x3fc00000, 0x7fc00001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR__,      FSR__,      FSR__,      FSR__
    /* 1 + SNaN = SNaN  */
    test_op2 add.s, f12, f13, f14, 0x3fc00000, 0x7f800001, \
        0x7f800001, 0x7f800001, 0x7f800001, 0x7f800001, \
             FSR__,      FSR__,      FSR__,      FSR__
    /* SNaN1 + SNaN2 = SNaN1 */
    test_op2 add.s, f15, f0, f1, 0x7f800001, 0x7fbfffff, \
        0x7f800001, 0x7f800001, 0x7f800001, 0x7f800001, \
             FSR__,      FSR__,      FSR__,      FSR__
    test_op2 add.s, f2, f3, f4, 0x7fbfffff, 0x7f800001, \
        0x7fbfffff, 0x7fbfffff, 0x7fbfffff, 0x7fbfffff, \
             FSR__,      FSR__,      FSR__,      FSR__
    /* QNaN1 + SNaN2 = QNaN1 */
    test_op2 add.s, f5, f6, f7, 0x7fc00001, 0x7fbfffff, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001, \
             FSR__,      FSR__,      FSR__,      FSR__
    /* SNaN1 + QNaN2 = SNaN1 */
    test_op2 add.s, f8, f9, f10, 0x7fbfffff, 0x7fc00001, \
        0x7fbfffff, 0x7fbfffff, 0x7fbfffff, 0x7fbfffff, \
             FSR__,      FSR__,      FSR__,      FSR__
test_end
#endif

test sub_s
    test_op2 sub.s, f0, f1, f0, 0x3f800001, 0x33800000, \
        0x3f800000, 0x3f800000, 0x3f800001, 0x3f800000, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
    test_op2 sub.s, f0, f1, f1, 0x3f800002, 0x33800000, \
        0x3f800002, 0x3f800001, 0x3f800002, 0x3f800001, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I

    /* norm - norm = denorm */
    test_op2 sub.s, f6, f7, f8, 0x00800001, 0x00800000, \
        0x00000001, 0x00000001, 0x00000001, 0x00000001, \
             FSR__,      FSR__,      FSR__,      FSR__
test_end

test mul_s
    test_op2 mul.s, f0, f1, f2, 0x3f800001, 0x3f800001, \
        0x3f800002, 0x3f800002, 0x3f800003, 0x3f800002, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
    /* MAX_FLOAT/2 * MAX_FLOAT/2 = +inf/MAX_FLOAT  */
    test_op2 mul.s, f6, f7, f8, 0x7f000000, 0x7f000000, \
        0x7f800000, 0x7f7fffff, 0x7f800000, 0x7f7fffff, \
            FSR_OI,     FSR_OI,     FSR_OI,     FSR_OI
    /* min norm * min norm = 0/denorm */
    test_op2 mul.s, f6, f7, f8, 0x00800001, 0x00800000, \
        0x00000000, 0x00000000, 0x00000001, 0x00000000, \
            FSR_UI,     FSR_UI,     FSR_UI,     FSR_UI
    /* inf * 0 = default NaN */
    test_op2 mul.s, f6, f7, f8, 0x7f800000, 0x00000000, \
        0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000, \
             FSR_V,      FSR_V,      FSR_V,      FSR_V
test_end

test madd_s
    test_op3 madd.s, f0, f1, f2, f0, 0, 0x3f800001, 0x3f800001, \
        0x3f800002, 0x3f800002, 0x3f800003, 0x3f800002, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
test_end

test madd_s_precision
    test_op3 madd.s, f0, f1, f2, f0, 0xbf800002, 0x3f800001, 0x3f800001, \
        0x28800000, 0x28800000, 0x28800000, 0x28800000, \
             FSR__,      FSR__,      FSR__,      FSR__
test_end

#if DFPU
test madd_s_nan_dfpu
    /* DFPU madd/msub NaN1, NaN2, NaN3 priority: NaN1, NaN3, NaN2 */
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_1, F32_1, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_QNAN(2), F32_1, \
        F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_1, F32_QNAN(3), \
        F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_QNAN(2), F32_1, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_1, F32_QNAN(3), \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_QNAN(2), F32_QNAN(3), \
        F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_QNAN(2), F32_QNAN(3), \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__

    /* inf * 0 = default NaN */
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_PINF, F32_0, \
        F32_DNAN, F32_DNAN, F32_DNAN, F32_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V
    /* inf * 0 + SNaN1 = QNaN1 */
    test_op3 madd.s, f0, f1, f2, f0, F32_SNAN(1), F32_PINF, F32_0, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    /* inf * 0 + QNaN1 = QNaN1 */
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_PINF, F32_0, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V

    /* madd/msub SNaN turns to QNaN and sets Invalid flag */
    test_op3 madd.s, f0, f1, f2, f0, F32_SNAN(1), F32_1, F32_1, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_SNAN(2), F32_1, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
test_end
#else
test madd_s_nan_fpu2k
    /* FPU2000 madd/msub NaN1, NaN2, NaN3 priority: NaN2, NaN3, NaN1 */
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_1, F32_1, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_QNAN(2), F32_1, \
        F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_1, F32_QNAN(3), \
        F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_QNAN(2), F32_1, \
        F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_1, F32_QNAN(3), \
        F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), F32_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_QNAN(2), F32_QNAN(3), \
        F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_QNAN(2), F32_QNAN(3), \
        F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), F32_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__

    /* inf * 0 = default NaN */
    test_op3 madd.s, f0, f1, f2, f0, F32_1, F32_PINF, F32_0, \
        F32_DNAN, F32_DNAN, F32_DNAN, F32_DNAN, \
           FSR__,    FSR__,    FSR__,    FSR__
    /* inf * 0 + SNaN1 = SNaN1 */
    test_op3 madd.s, f0, f1, f2, f0, F32_SNAN(1), F32_PINF, F32_0, \
        F32_SNAN(1), F32_SNAN(1), F32_SNAN(1), F32_SNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    /* inf * 0 + QNaN1 = QNaN1 */
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_PINF, F32_0, \
        F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), F32_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__

    /* madd/msub SNaN is preserved */
    test_op3 madd.s, f0, f1, f2, f0, F32_SNAN(1), F32_1, F32_1, \
        F32_SNAN(1), F32_SNAN(1), F32_SNAN(1), F32_SNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.s, f0, f1, f2, f0, F32_QNAN(1), F32_SNAN(2), F32_1, \
        F32_SNAN(2), F32_SNAN(2), F32_SNAN(2), F32_SNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__
test_end
#endif

test msub_s
    test_op3 msub.s, f0, f1, f2, f0, 0x3f800000, 0x3f800001, 0x3f800001, \
        0xb4800000, 0xb4800000, 0xb4800000, 0xb4800001, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
test_end

#endif

test_suite_end
