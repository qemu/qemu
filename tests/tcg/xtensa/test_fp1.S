#include "macros.inc"
#include "fpu.h"

test_suite fp1

#if XCHAL_HAVE_FP

.macro movfp fr, v
    movi    a2, \v
    wfr     \fr, a2
.endm

.macro test_ord_ex op, br, fr0, fr1, v0, v1, r, sr
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    \op     \br, \fr0, \fr1
    movi    a2, 0
    movi    a3, 1
    movt    a2, a3, \br
    assert  eqi, a2, \r
    rur     a2, fsr
#if DFPU
    movi    a3, \sr
    assert  eq, a2, a3
#else
    assert  eqi, a2, 0
#endif
.endm

.macro test_ord op, br, fr0, fr1, v0, v1, r, sr
    movi    a2, 0
    wur     a2, fcr
    test_ord_ex \op, \br, \fr0, \fr1, \v0, \v1, \r, \sr
    movi    a2, 0x7c
    wur     a2, fcr
    test_ord_ex \op, \br, \fr0, \fr1, \v0, \v1, \r, \sr
.endm

.macro test_ord_all op, aa, ab, ba, aPI, PIa, aN, Na, II, IN, NI, qnan_sr
    test_ord \op  b0,  f0,  f1, 0x3f800000, 0x3f800000, \aa,  FSR__    /*   ord == ord */
    test_ord \op  b1,  f2,  f3, 0x3f800000, 0x3fc00000, \ab,  FSR__    /*   ord <  ord */
    test_ord \op  b2,  f4,  f5, 0x3fc00000, 0x3f800000, \ba,  FSR__    /*   ord >  ord */
    test_ord \op  b3,  f6,  f7, 0x3f800000, 0x7f800000, \aPI, FSR__    /*   ord   +INF */
    test_ord \op  b4,  f8,  f9, 0x7f800000, 0x3f800000, \PIa, FSR__    /*  +INF    ord */
    test_ord \op  b5, f10, f11, 0x3f800000, 0xffc00001, \aN,  \qnan_sr /*   ord  -QNaN */
    test_ord \op  b6, f12, f13, 0x3f800000, 0xff800001, \aN,  FSR_V    /*   ord  -SNaN */
    test_ord \op  b7, f14, f15, 0x3f800000, 0x7f800001, \aN,  FSR_V    /*   ord  +SNaN */
    test_ord \op  b8,  f0,  f1, 0x3f800000, 0x7fc00000, \aN,  \qnan_sr /*   ord  +QNaN */
    test_ord \op  b9,  f2,  f3, 0xffc00001, 0x3f800000, \Na,  \qnan_sr /* -QNaN    ord */
    test_ord \op b10,  f4,  f5, 0xff800001, 0x3f800000, \Na,  FSR_V    /* -SNaN    ord */
    test_ord \op b11,  f6,  f7, 0x7f800001, 0x3f800000, \Na,  FSR_V    /* +SNaN    ord */
    test_ord \op b12,  f8,  f9, 0x7fc00000, 0x3f800000, \Na,  \qnan_sr /* +QNaN    ord */
    test_ord \op b13, f10, f11, 0x7f800000, 0x7f800000, \II,  FSR__    /*  +INF   +INF */
    test_ord \op b14, f12, f13, 0x7f800000, 0x7fc00000, \IN,  \qnan_sr /*  +INF  +QNaN */
    test_ord \op b15, f14, f15, 0x7fc00000, 0x7f800000, \NI,  \qnan_sr /* +QNaN   +INF */
.endm

test un_s
    movi    a2, 1
    wsr     a2, cpenable
    test_ord_all un.s, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, FSR__
test_end

test oeq_s
    test_ord_all oeq.s, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, FSR__
test_end

test ueq_s
    test_ord_all ueq.s, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, FSR__
test_end

test olt_s
    test_ord_all olt.s, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, FSR_V
test_end

test ult_s
    test_ord_all ult.s, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, FSR__
test_end

test ole_s
    test_ord_all ole.s, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, FSR_V
test_end

test ule_s
    test_ord_all ule.s, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, FSR__
test_end

.macro test_cond op, fr0, fr1, cr, v0, v1, r
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    \op     \fr0, \fr1, \cr
    rfr     a2, \fr0
    movi    a3, \r
    assert  eq, a2, a3
.endm

test moveqz_s
    movi    a3, 0
    test_cond moveqz.s, f0, f1, a3, 0, 0x3f800000, 0x3f800000
    movi    a3, 1
    test_cond moveqz.s, f0, f1, a3, 0, 0x3f800000, 0
test_end

test movnez_s
    movi    a3, 0
    test_cond movnez.s, f0, f1, a3, 0, 0x3f800000, 0
    movi    a3, 1
    test_cond movnez.s, f0, f1, a3, 0, 0x3f800000, 0x3f800000
test_end

test movltz_s
    movi    a3, -1
    test_cond movltz.s, f0, f1, a3, 0, 0x3f800000, 0x3f800000
    movi    a3, 0
    test_cond movltz.s, f0, f1, a3, 0, 0x3f800000, 0
    movi    a3, 1
    test_cond movltz.s, f0, f1, a3, 0, 0x3f800000, 0
test_end

test movgez_s
    movi    a3, -1
    test_cond movgez.s, f0, f1, a3, 0, 0x3f800000, 0
    movi    a3, 0
    test_cond movgez.s, f0, f1, a3, 0, 0x3f800000, 0x3f800000
    movi    a3, 1
    test_cond movgez.s, f0, f1, a3, 0, 0x3f800000, 0x3f800000
test_end

test movf_s
    olt.s   b0, f0, f0
    test_cond movf.s, f0, f1, b0, 0, 0x3f800000, 0x3f800000
    ueq.s   b0, f0, f0
    test_cond movf.s, f0, f1, b0, 0, 0x3f800000, 0
test_end

test movt_s
    ueq.s   b0, f0, f0
    test_cond movt.s, f0, f1, b0, 0, 0x3f800000, 0x3f800000
    olt.s   b0, f0, f0
    test_cond movt.s, f0, f1, b0, 0, 0x3f800000, 0
test_end

#endif

test_suite_end
