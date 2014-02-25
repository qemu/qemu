#include "macros.inc"

test_suite max

test max
    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    max     a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    max     a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    max     a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 1
    max     a3, a2, a3
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    max     a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 1
    max     a3, a2, a3
    assert  eq, a3, a4
test_end

test maxu
    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    maxu    a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    maxu    a5, a2, a3
    assert  eq, a5, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    maxu    a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 0xffffffff
    movi    a3, 1
    movi    a4, 0xffffffff
    maxu    a3, a2, a3
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    maxu    a2, a2, a3
    assert  eq, a2, a4

    movi    a2, 1
    movi    a3, 0xffffffff
    movi    a4, 0xffffffff
    maxu    a3, a2, a3
    assert  eq, a3, a4
test_end

test_suite_end
