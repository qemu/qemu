.include "macros.inc"

test_suite clamps

test clamps
    movi    a2, 0
    movi    a3, 0
    clamps  a4, a2, 7
    assert  eq, a3, a4

    movi    a2, 0x7f
    movi    a3, 0x7f
    clamps  a4, a2, 7
    assert  eq, a3, a4

    movi    a2, 0xffffff80
    movi    a3, 0xffffff80
    clamps  a4, a2, 7
    assert  eq, a3, a4

    movi    a2, 0x80
    movi    a3, 0x7f
    clamps  a2, a2, 7
    assert  eq, a3, a2

    movi    a2, 0xffffff7f
    movi    a3, 0xffffff80
    clamps  a2, a2, 7
    assert  eq, a3, a2

    movi    a2, 0x7fffffff
    movi    a3, 0x7f
    clamps  a2, a2, 7
    assert  eq, a3, a2

    movi    a2, 0x80000000
    movi    a3, 0xffffff80
    clamps  a2, a2, 7
    assert  eq, a3, a2
test_end

test_suite_end
