#include "macros.inc"
#include "fpu.h"

test_suite fp0_sqrt

#if XCHAL_HAVE_FP_SQRT

.macro  sqrt_seq r, a, y, t1, hn, h2, t5, h
    sqrt0.s     \y, \a
    const.s     \t1, 0
    maddn.s     \t1, \y, \y
    nexp01.s    \hn, \a
    const.s     \r, 3
    addexp.s    \hn, \r
    maddn.s     \r, \t1, \hn
    nexp01.s    \t1, \a
    neg.s       \h2, \t1
    maddn.s     \y, \r, \y
    const.s     \r, 0
    const.s     \t5, 0
    const.s     \h, 0
    maddn.s     \r, \h2, \y
    maddn.s     \t5, \y, \hn
    const.s     \hn, 3
    maddn.s     \h, \hn, \y
    maddn.s     \t1, \r, \r
    maddn.s     \hn, \t5, \y
    neg.s       \y, \h
    maddn.s     \r, \t1, \y
    maddn.s     \h, \hn, \h
    mksadj.s    \y, \a
    nexp01.s    \a, \a
    maddn.s     \a, \r, \r
    neg.s       \t1, \h
    addexpm.s   \r, \y
    addexp.s    \t1, \y
    divn.s      \r, \a, \t1
.endm

.macro sqrt_s fr0, fr1
    sqrt_seq    \fr0, \fr1, f10, f11, f12, f13, f14, f15
.endm

.macro movfp fr, v
    movi        a2, \v
    wfr         \fr, a2
.endm

.macro check_res fr, r, sr
    rfr         a2, \fr
    dump        a2
    movi        a3, \r
    assert      eq, a2, a3
    rur         a2, fsr
    movi        a3, \sr
    assert      eq, a2, a3
.endm

test sqrt_s
    movi        a2, 1
    wsr         a2, cpenable

    test_op1    sqrt_s, f0, f1, 0x40000000, \
        0x3fb504f3, 0x3fb504f3, 0x3fb504f4, 0x3fb504f3, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
    test_op1    sqrt_s, f3, f4, F32_1, \
        F32_1, F32_1, F32_1, F32_1, \
        FSR__, FSR__, FSR__, FSR__
    test_op1    sqrt_s, f6, f7, F32_MINUS | F32_1, \
        F32_DNAN, F32_DNAN, F32_DNAN, F32_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V
test_end

#endif

test_suite_end
