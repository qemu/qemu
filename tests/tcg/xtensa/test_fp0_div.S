#include "macros.inc"
#include "fpu.h"

test_suite fp0_div

#if XCHAL_HAVE_FP_DIV

.macro  divs_seq q, a, b, r, y, y0, an, bn, e, ex
    div0.s      \y0, \b
    nexp01.s    \bn, \b
    const.s     \e, 1
    maddn.s     \e, \bn, \y0
    mov.s       \y, \y0
    mov.s       \ex, \b
    nexp01.s    \an, \a
    maddn.s     \y, \e, \y0
    const.s     \e, 1
    const.s     \q, 0
    neg.s       \r, \an
    maddn.s     \e, \bn, \y
    maddn.s     \q, \r, \y0
    mkdadj.s    \ex, \a
    maddn.s     \y, \e, \y
    maddn.s     \r, \bn, \q
    const.s     \e, 1
    maddn.s     \e, \bn, \y
    maddn.s     \q, \r, \y
    neg.s       \r, \an
    maddn.s     \y, \e, \y
    maddn.s     \r, \bn, \q
    addexpm.s   \q, \ex
    addexp.s    \y, \ex
    divn.s      \q, \r, \y
.endm

.macro div_s fr0, fr1, fr2
    divs_seq    \fr0, \fr1, \fr2, f9, f10, f11, f12, f13, f14, f15
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

test div_s
    movi        a2, 1
    wsr         a2, cpenable

    test_op2    div_s, f0, f1, f2, 0x40000000, 0x40400000, \
        0x3f2aaaab, 0x3f2aaaaa, 0x3f2aaaab, 0x3f2aaaaa, \
             FSR_I,      FSR_I,      FSR_I,      FSR_I
    test_op2    div_s, f3, f4, f5, F32_1, F32_0, \
        F32_PINF, F32_PINF, F32_PINF, F32_PINF, \
           FSR_Z,    FSR_Z,    FSR_Z,    FSR_Z
    test_op2    div_s, f6, f7, f8, F32_0, F32_0, \
        F32_DNAN, F32_DNAN, F32_DNAN, F32_DNAN, \
           FSR_V,    FSR_V,    FSR_V,    FSR_V

    /* MAX_FLOAT / 0.5 = +inf/MAX_FLOAT  */
    test_op2    div_s, f0, f1, f2, F32_MAX, F32_0_5, \
        F32_PINF, F32_MAX, F32_PINF, F32_MAX, \
          FSR_OI,  FSR_OI,   FSR_OI,  FSR_OI

    /* 0.5 / MAX_FLOAT = denorm  */
    test_op2    div_s, f0, f1, f2, F32_0_5, F32_MAX, \
        0x00100000, 0x00100000, 0x00100001, 0x00100000, \
            FSR_UI,     FSR_UI,     FSR_UI,     FSR_UI
test_end

#endif

test_suite_end
