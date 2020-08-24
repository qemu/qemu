#include "macros.inc"
#include "fpu.h"

test_suite lsc

#if XCHAL_HAVE_FP

test lsi
    movi    a2, 1
    wsr     a2, cpenable

    movi    a2, 1f
    lsi     f1, a2, 4
#if DFPU
    lsi     f2, a2, 8
    lsip    f0, a2, 8
#else
    lsi     f0, a2, 0
    lsiu    f2, a2, 8
#endif
    movi    a3, 1f + 8
    assert  eq, a2, a3
    rfr     a2, f0
    movi    a3, 0x3f800000
    assert  eq, a2, a3
    rfr     a2, f1
    movi    a3, 0x40000000
    assert  eq, a2, a3
    rfr     a2, f2
    movi    a3, 0x40400000
    assert  eq, a2, a3
.data
    .align  4
1:
.float 1, 2, 3
.text
test_end

test ssi
    movi    a2, 1f
    movi    a3, 0x40800000
    wfr     f3, a3
    movi    a3, 0x40a00000
    wfr     f4, a3
    movi    a3, 0x40c00000
    wfr     f5, a3
    ssi     f4, a2, 4
#if DFPU
    ssi     f5, a2, 8
    ssip    f3, a2, 8
#else
    ssi     f3, a2, 0
    ssiu    f5, a2, 8
#endif
    movi    a3, 1f + 8
    assert  eq, a2, a3
    l32i    a4, a2, -8
    movi    a3, 0x40800000
    assert  eq, a4, a3
    l32i    a4, a2, -4
    movi    a3, 0x40a00000
    assert  eq, a4, a3
    l32i    a4, a2, 0
    movi    a3, 0x40c00000
    assert  eq, a4, a3
.data
    .align  4
1:
.float 0, 0, 0
.text
test_end

test lsx
    movi    a2, 1f
    movi    a3, 0
    movi    a4, 4
    movi    a5, 8
    lsx     f7, a2, a4
#if DFPU
    lsx     f8, a2, a5
    lsxp    f6, a2, a5
#else
    lsx     f6, a2, a3
    lsxu    f8, a2, a5
#endif
    movi    a3, 1f + 8
    assert  eq, a2, a3
    rfr     a2, f6
    movi    a3, 0x40e00000
    assert  eq, a2, a3
    rfr     a2, f7
    movi    a3, 0x41000000
    assert  eq, a2, a3
    rfr     a2, f8
    movi    a3, 0x41100000
    assert  eq, a2, a3
.data
    .align  4
1:
.float 7, 8, 9
.text
test_end

test ssx
    movi    a2, 1f
    movi    a4, 0x41200000
    wfr     f9, a4
    movi    a4, 0x41300000
    wfr     f10, a4
    movi    a4, 0x41400000
    wfr     f11, a4
    movi    a3, 0
    movi    a4, 4
    movi    a5, 8
    ssx     f10, a2, a4
#if DFPU
    ssx     f11, a2, a5
    ssxp    f9, a2, a5
#else
    ssx     f9, a2, a3
    ssxu    f11, a2, a5
#endif
    movi    a3, 1f + 8
    assert  eq, a2, a3
    l32i    a4, a2, -8
    movi    a3, 0x41200000
    assert  eq, a4, a3
    l32i    a4, a2, -4
    movi    a3, 0x41300000
    assert  eq, a4, a3
    l32i    a4, a2, 0
    movi    a3, 0x41400000
    assert  eq, a4, a3
.data
    .align  4
1:
.float 0, 0, 0
.text
test_end

#endif

#if XCHAL_HAVE_DFP

#if XCHAL_HAVE_BE
#define F64_HIGH_OFF 0
#else
#define F64_HIGH_OFF 4
#endif

.macro movdf fr, hi, lo
    movi    a2, \hi
    movi    a3, \lo
    wfrd    \fr, a2, a3
.endm

test ldi
    movi    a2, 1
    wsr     a2, cpenable

    movi    a2, 1f
    ldi     f1, a2, 8
    ldi     f2, a2, 16
    ldip    f0, a2, 16
    movi    a3, 1f + 16
    assert  eq, a2, a3
    rfrd    a2, f0
    movi    a3, 0x3ff00000
    assert  eq, a2, a3
    rfrd    a2, f1
    movi    a3, 0x40000000
    assert  eq, a2, a3
    rfrd    a2, f2
    movi    a3, 0x40080000
    assert  eq, a2, a3
.data
    .align  8
1:
.double 1, 2, 3
.text
test_end

test sdi
    movdf   f3, 0x40800000, 0
    movdf   f4, 0x40a00000, 0
    movdf   f5, 0x40c00000, 0
    movi    a2, 1f
    sdi     f4, a2, 8
    sdi     f5, a2, 16
    sdip    f3, a2, 16
    movi    a3, 1f + 16
    assert  eq, a2, a3
    l32i    a4, a2, -16 + F64_HIGH_OFF
    movi    a3, 0x40800000
    assert  eq, a4, a3
    l32i    a4, a2, -8 + F64_HIGH_OFF
    movi    a3, 0x40a00000
    assert  eq, a4, a3
    l32i    a4, a2, F64_HIGH_OFF
    movi    a3, 0x40c00000
    assert  eq, a4, a3
.data
    .align  8
1:
.double 0, 0, 0
.text
test_end

test ldx
    movi    a2, 1f
    movi    a3, 0
    movi    a4, 8
    movi    a5, 16
    ldx     f7, a2, a4
    ldx     f8, a2, a5
    ldxp    f6, a2, a5
    movi    a3, 1f + 16
    assert  eq, a2, a3
    rfrd    a2, f6
    movi    a3, 0x401c0000
    assert  eq, a2, a3
    rfrd    a2, f7
    movi    a3, 0x40200000
    assert  eq, a2, a3
    rfrd    a2, f8
    movi    a3, 0x40220000
    assert  eq, a2, a3
.data
    .align  8
1:
.double 7, 8, 9
.text
test_end

test sdx
    movdf   f9, 0x41200000, 0
    movdf   f10, 0x41300000, 0
    movdf   f11, 0x41400000, 0
    movi    a2, 1f
    movi    a3, 0
    movi    a4, 8
    movi    a5, 16
    sdx     f10, a2, a4
    sdx     f11, a2, a5
    sdxp    f9, a2, a5
    movi    a3, 1f + 16
    assert  eq, a2, a3
    l32i    a4, a2, -16 + F64_HIGH_OFF
    movi    a3, 0x41200000
    assert  eq, a4, a3
    l32i    a4, a2, -8 + F64_HIGH_OFF
    movi    a3, 0x41300000
    assert  eq, a4, a3
    l32i    a4, a2, F64_HIGH_OFF
    movi    a3, 0x41400000
    assert  eq, a4, a3
.data
    .align  8
1:
.double 0, 0, 0
.text
test_end

#endif

test_suite_end
