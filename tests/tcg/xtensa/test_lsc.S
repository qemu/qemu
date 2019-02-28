#include "macros.inc"

test_suite lsc

#if XCHAL_HAVE_FP

test lsi
    movi    a2, 1
    wsr     a2, cpenable

    movi    a2, 1f
    lsi     f0, a2, 0
    lsi     f1, a2, 4
    lsiu    f2, a2, 8
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
    ssi     f3, a2, 0
    movi    a3, 0x40a00000
    wfr     f4, a3
    ssi     f4, a2, 4
    movi    a3, 0x40c00000
    wfr     f5, a3
    ssiu    f5, a2, 8
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
    lsx     f6, a2, a3
    movi    a3, 4
    lsx     f7, a2, a3
    movi    a3, 8
    lsxu    f8, a2, a3
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
    movi    a3, 0
    movi    a4, 0x41200000
    wfr     f9, a4
    ssx     f9, a2, a3
    movi    a3, 4
    movi    a4, 0x41300000
    wfr     f10, a4
    ssx     f10, a2, a3
    movi    a3, 8
    movi    a4, 0x41400000
    wfr     f11, a4
    ssxu    f11, a2, a3
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

test_suite_end
