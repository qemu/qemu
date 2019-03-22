#include "macros.inc"

test_suite flix

#if XCHAL_HAVE_FLIX3

test misc
    {
        mov     a3, a4
        mov     a2, a3
        nop
    }
    {
        nop
        bne.w18 a2, a3, 1f
    }
    movi    a2, 1f
    {
        mov     a2, a3
        mov     a3, a2
        nop
    }
    {
        l32i    a2, a3, 0
        add     a4, a4, a2
        nop
    }
    {
        mov     a3, a4
        jx      a3
        nop
    }
1:
test_end

test sum

    movi    a2, 0
    movi    a3, 2f
    movi    a4, 0
    movi    a5, 4

    loop    a5, 1f
    {
        l32i    a2, a3, 0
        addi    a3, a3, 4
        add     a4, a4, a2
    }
1:
    add     a4, a4, a2
    assert  eqi, a4, 10
    .data
2:
    .word   1, 2, 3, 4
    .previous
test_end

test rep_dependency

    {
        movi    a2, 1
        movi    a3, 2
        nop
    }
    {
        or      a2, a3, a3
        or      a3, a2, a2
        nop
    }
    assert  eqi, a2, 2
    assert  eqi, a3, 1

test_end

#endif

test_suite_end
