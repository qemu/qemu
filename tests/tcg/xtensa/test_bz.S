.include "macros.inc"

test_suite bz

test beqz
    movi    a2, 0
    _beqz   a2, 1f
    test_fail
1:
    movi    a2, 1
    _beqz   a2, 1f
    j       2f
1:
    test_fail
2:
test_end

test bnez
    movi    a2, 1
    _bnez   a2, 1f
    test_fail
1:
    movi    a2, 0
    _bnez   a2, 1f
    j       2f
1:
    test_fail
2:
test_end

test bltz
    movi    a2, 0xffffffff
    bltz    a2, 1f
    test_fail
1:
    movi    a2, 0
    bltz    a2, 1f
    j       2f
1:
    test_fail
2:
test_end

test bgez
    movi    a2, 0
    bgez    a2, 1f
    test_fail
1:
    movi    a2, 0xffffffff
    bgez    a2, 1f
    j       2f
1:
    test_fail
2:
test_end

test_suite_end
