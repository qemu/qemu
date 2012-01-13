.include "macros.inc"

test_suite bi

test beqi
    movi    a2, 7
    beqi    a2, 7, 1f
    test_fail
1:
    movi    a2, 1
    beqi    a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test bnei
    movi    a2, 1
    bnei    a2, 7, 1f
    test_fail
1:
    movi    a2, 7
    bnei    a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test blti
    movi    a2, 6
    blti    a2, 7, 1f
    test_fail
1:
    movi    a2, 0xffffffff
    blti    a2, 7, 1f
    test_fail
1:
    movi    a2, 7
    blti    a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test bgei
    movi    a2, 7
    bgei    a2, 7, 1f
    test_fail
1:
    movi    a2, 6
    bgei    a2, 7, 1f
    j       2f
1:
    test_fail
2:
    movi    a2, 0xffffffff
    bgei    a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test bltui
    movi    a2, 6
    bltui   a2, 7, 1f
    test_fail
1:
    movi    a2, 7
    bltui   a2, 7, 1f
    j       2f
1:
    test_fail
2:
    movi    a2, 0xffffffff
    bltui   a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test bgeui
    movi    a2, 7
    bgeui   a2, 7, 1f
    test_fail
1:
    movi    a2, 0xffffffff
    bgeui   a2, 7, 1f
    test_fail
1:
    movi    a2, 6
    bgeui   a2, 7, 1f
    j       2f
1:
    test_fail
2:
test_end

test_suite_end
