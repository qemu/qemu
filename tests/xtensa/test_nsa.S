.include "macros.inc"

test_suite nsa

test nsa
    movi    a2, 0
    movi    a3, 31
    nsa     a4, a2
    assert  eq, a3, a4

    movi    a2, 0xffffffff
    movi    a3, 31
    nsa     a4, a2
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 30
    nsa     a2, a2
    assert  eq, a3, a2

    movi    a2, 0xfffffffe
    movi    a3, 30
    nsa     a2, a2
    assert  eq, a3, a2

    movi    a2, 0x5a5a5a5a
    movi    a3, 0
    nsa     a4, a2
    assert  eq, a3, a4

    movi    a2, 0xa5a5a5a5
    movi    a3, 0
    nsa     a4, a2
    assert  eq, a3, a4
test_end

test nsau
    movi    a2, 0
    movi    a3, 32
    nsau    a4, a2
    assert  eq, a3, a4

    movi    a2, 0xffffffff
    movi    a3, 0
    nsau    a4, a2
    assert  eq, a3, a4

    movi    a2, 1
    movi    a3, 31
    nsau    a2, a2
    assert  eq, a3, a2

    movi    a2, 0x5a5a5a5a
    movi    a3, 1
    nsau    a2, a2
    assert  eq, a3, a2
test_end

test_suite_end
