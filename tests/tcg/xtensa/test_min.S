#include "macros.inc"

test_suite min

test min
    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    min     a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    min     a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    min     a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    min     a3, a2, a3
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    min     a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    min     a3, a2, a3
    assert  eq, a3, a4
test_end

test minu
    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    minu    a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    minu    a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    minu    a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    minu    a3, a2, a3
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    minu    a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    minu    a3, a2, a3
    assert  eq, a3, a4
test_end

test_suite_end
