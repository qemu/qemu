#include "macros.inc"

test_suite fp_cpenable

#if XCHAL_HAVE_FP

test rur
    set_vector kernel, 2f
    movi    a2, 0
    wsr     a2, cpenable
    isync
1:
    rur     a2, fsr
    //wfr     f0, a2
    test_fail
2:
    movi    a2, 1b
    rsr     a3, epc1
    assert  eq, a2, a3
    movi    a2, 32
    rsr     a3, exccause
    assert  eq, a2, a3
test_end

#endif

test_suite_end
