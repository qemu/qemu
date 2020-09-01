#include "macros.inc"
#include "fpu.h"

test_suite fp0_arith

#if XCHAL_HAVE_DFP

.macro movfp fr, v
    movi    a2, ((\v) >> 32) & 0xffffffff
    movi    a3, ((\v) & 0xffffffff)
    wfrd    \fr, a2, a3
.endm

.macro check_res fr, r, sr
    rfrd    a2, \fr
    dump    a2
    movi    a3, ((\r) >> 32) & 0xffffffff
    assert  eq, a2, a3
    rfr    a2, \fr
    dump    a2
    movi    a3, ((\r) & 0xffffffff)
    assert  eq, a2, a3
    rur     a2, fsr
    movi    a3, \sr
    assert  eq, a2, a3
.endm

test add_d
    movi    a2, 1
    wsr     a2, cpenable

    /* MAX_FLOAT + MAX_FLOAT = +inf/MAX_FLOAT  */
    test_op2 add.d, f6, f7, f8, F64_MAX, F64_MAX, \
        F64_PINF, F64_MAX, F64_PINF, F64_MAX, \
          FSR_OI,  FSR_OI,   FSR_OI,  FSR_OI
test_end

test add_d_inf
    /* 1 + +inf = +inf  */
    test_op2 add.d, f6, f7, f8, F64_1, F64_PINF, \
        F64_PINF, F64_PINF, F64_PINF, F64_PINF, \
           FSR__,    FSR__,    FSR__,    FSR__

    /* +inf + -inf = default NaN */
    test_op2 add.d, f0, f1, f2, F64_PINF, F64_NINF, \
        F64_DNAN, F64_DNAN, F64_DNAN, F64_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V
test_end

test add_d_nan_dfpu
    /* 1 + QNaN = QNaN  */
    test_op2 add.d, f9, f10, f11, F64_1, F64_QNAN(1), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    /* 1 + SNaN = QNaN  */
    test_op2 add.d, f12, f13, f14, F64_1, F64_SNAN(1), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V

    /* SNaN1 + SNaN2 = QNaN2 */
    test_op2 add.d, f15, f0, f1, F64_SNAN(1), F64_SNAN(2), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    /* QNaN1 + SNaN2 = QNaN2 */
    test_op2 add.d, f5, f6, f7, F64_QNAN(1), F64_SNAN(2), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    /* SNaN1 + QNaN2 = QNaN2 */
    test_op2 add.d, f8, f9, f10, F64_SNAN(1), F64_QNAN(2), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
test_end

test sub_d
    /* norm - norm = denorm */
    test_op2 sub.d, f6, f7, f8, F64_MIN_NORM | 1, F64_MIN_NORM, \
        0x00000001, 0x00000001, 0x00000001, 0x00000001, \
             FSR__,      FSR__,      FSR__,      FSR__
test_end

test mul_d
    test_op2 mul.d, f0, f1, f2, F64_1 | 1, F64_1 | 1, \
        F64_1 | 2, F64_1 | 2, F64_1 | 3, F64_1 | 2, \
            FSR_I,     FSR_I,     FSR_I,     FSR_I
    /* MAX_FLOAT/2 * MAX_FLOAT/2 = +inf/MAX_FLOAT  */
    test_op2 mul.d, f6, f7, f8, F64_MAX_2, F64_MAX_2, \
        F64_PINF, F64_MAX, F64_PINF, F64_MAX, \
          FSR_OI,  FSR_OI,   FSR_OI,  FSR_OI
    /* min norm * min norm = 0/denorm */
    test_op2 mul.d, f6, f7, f8, F64_MIN_NORM, F64_MIN_NORM, \
         F64_0,  F64_0, 0x00000001,  F64_0, \
        FSR_UI, FSR_UI,     FSR_UI, FSR_UI
    /* inf * 0 = default NaN */
    test_op2 mul.d, f6, f7, f8, F64_PINF, F64_0, \
        F64_DNAN, F64_DNAN, F64_DNAN, F64_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V
test_end

test madd_d
    test_op3 madd.d, f0, f1, f2, f0, F64_0, F64_1 | 1, F64_1 | 1, \
        F64_1 | 2, F64_1 | 2, F64_1 | 3, F64_1 | 2, \
            FSR_I,     FSR_I,     FSR_I,     FSR_I
test_end

test madd_d_precision
    test_op3 madd.d, f0, f1, f2, f0, \
        F64_MINUS | F64_1 | 2, F64_1 | 1, F64_1 | 1, \
        0x3970000000000000, 0x3970000000000000, 0x3970000000000000, 0x3970000000000000, \
             FSR__,      FSR__,      FSR__,      FSR__
test_end

test madd_d_nan_dfpu
    /* DFPU madd/msub NaN1, NaN2, NaN3 priority: NaN1, NaN3, NaN2 */
    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_1, F64_1, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.d, f0, f1, f2, f0, F64_1, F64_QNAN(2), F64_1, \
        F64_QNAN(2), F64_QNAN(2), F64_QNAN(2), F64_QNAN(2), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.d, f0, f1, f2, f0, F64_1, F64_1, F64_QNAN(3), \
        F64_QNAN(3), F64_QNAN(3), F64_QNAN(3), F64_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_QNAN(2), F64_1, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_1, F64_QNAN(3), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__
    test_op3 madd.d, f0, f1, f2, f0, F64_1, F64_QNAN(2), F64_QNAN(3), \
        F64_QNAN(3), F64_QNAN(3), F64_QNAN(3), F64_QNAN(3), \
              FSR__,       FSR__,       FSR__,       FSR__

    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_QNAN(2), F64_QNAN(3), \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR__,       FSR__,       FSR__,       FSR__

    /* inf * 0 = default NaN */
    test_op3 madd.d, f0, f1, f2, f0, F64_1, F64_PINF, F64_0, \
        F64_DNAN, F64_DNAN, F64_DNAN, F64_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V
    /* inf * 0 + SNaN1 = QNaN1 */
    test_op3 madd.d, f0, f1, f2, f0, F64_SNAN(1), F64_PINF, F64_0, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    /* inf * 0 + QNaN1 = QNaN1 */
    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_PINF, F64_0, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V

    /* madd/msub SNaN turns to QNaN and sets Invalid flag */
    test_op3 madd.d, f0, f1, f2, f0, F64_SNAN(1), F64_1, F64_1, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
    test_op3 madd.d, f0, f1, f2, f0, F64_QNAN(1), F64_SNAN(2), F64_1, \
        F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), F64_QNAN(1), \
              FSR_V,       FSR_V,       FSR_V,       FSR_V
test_end

#endif

test_suite_end
