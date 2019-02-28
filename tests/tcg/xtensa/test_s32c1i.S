#include "macros.inc"

test_suite s32c1i

#if XCHAL_HAVE_S32C1I

test s32c1i_nowrite
#if XCHAL_HW_VERSION >= 230000
    movi    a2, 0x29
    wsr     a2, atomctl
#endif
    movi    a2, 1f
    movi    a3, 1
    wsr     a3, scompare1
    movi    a1, 2
    s32c1i  a1, a2, 0
    assert  ne, a1, a3
    l32i    a1, a2, 0
    assert  eqi, a1, 3

.data
.align 4
1:
    .word   3
.text
test_end

test s32c1i_write
#if XCHAL_HW_VERSION >= 230000
    movi    a2, 0x29
    wsr     a2, atomctl
#endif
    movi    a2, 1f
    movi    a3, 3
    wsr     a3, scompare1
    movi    a1, 2
    s32c1i  a1, a2, 0
    assert  eq, a1, a3
    l32i    a1, a2, 0
    assert  eqi, a1, 2

.data
.align 4
1:
    .word   3
.text
test_end

#endif

test_suite_end
