#include "macros.inc"

test_suite exclusive

#if XCHAL_HAVE_EXCLUSIVE

test exclusive_nowrite
    movi    a2, 0x29
    wsr     a2, atomctl
    clrex
    movi    a2, 1f
    movi    a3, 1
    s32ex   a3, a2
    getex   a3
    assert  eqi, a3, 0
    l32i    a3, a2, 0
    assert  eqi, a3, 3

.data
.align 4
1:
    .word   3
.text
test_end

test exclusive_write
    movi    a2, 0x29
    wsr     a2, atomctl
    movi    a2, 1f
    l32ex   a3, a2
    assert  eqi, a3, 3
    movi    a3, 2
    s32ex   a3, a2
    getex   a3
    assert  eqi, a3, 1
    l32i    a3, a2, 0
    assert  eqi, a3, 2

.data
.align 4
1:
    .word   3
.text
test_end

#endif

test_suite_end
