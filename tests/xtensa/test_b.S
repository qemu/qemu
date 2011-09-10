.include "macros.inc"

test_suite b

test bnone
    movi    a2, 0xa5a5ff00
    movi    a3, 0x5a5a00ff
    bnone   a2, a3, 1f
    test_fail
1:
    movi    a2, 0xa5a5ff01
    bnone   a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test beq
    movi    a2, 0
    movi    a3, 0
    beq     a2, a3, 1f
    test_fail
1:
    movi    a2, 1
    beq     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test blt
    movi    a2, 6
    movi    a3, 7
    blt     a2, a3, 1f
    test_fail
1:
    movi    a2, 0xffffffff
    blt     a2, a3, 1f
    test_fail
1:
    movi    a2, 7
    blt     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bltu
    movi    a2, 6
    movi    a3, 7
    bltu    a2, a3, 1f
    test_fail
1:
    movi    a2, 7
    bltu    a2, a3, 1f
    j       2f
1:
    test_fail
2:
    movi    a2, 0xffffffff
    bltu    a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test ball
    movi    a2, 0xa5a5ffa5
    movi    a3, 0xa5a5ff00
    ball    a2, a3, 1f
    test_fail
1:
    movi    a2, 0xa5a5a5a5
    ball    a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bbc
    movi    a2, 0xfffffffd
    movi    a3, 0xffffff01
    bbc     a2, a3, 1f
    test_fail
1:
    movi    a2, 8
    movi    a3, 0xffffff03
    bbc     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bbci
    movi    a2, 0xfffdffff
    bbci    a2, 17, 1f
    test_fail
1:
    movi    a2, 0x00020000
    bbci    a2, 17, 1f
    j       2f
1:
    test_fail
2:
test_end

test bany
    movi    a2, 0xa5a5ff01
    movi    a3, 0x5a5a00ff
    bany    a2, a3, 1f
    test_fail
1:
    movi    a2, 0xa5a5ff00
    bany    a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bne
    movi    a2, 1
    movi    a3, 0
    bne     a2, a3, 1f
    test_fail
1:
    movi    a2, 0
    bne     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bge
    movi    a2, 7
    movi    a3, 7
    bge     a2, a3, 1f
    test_fail
1:
    movi    a2, 6
    bge     a2, a3, 1f
    j       2f
1:
    test_fail
2:
    movi    a2, 0xffffffff
    bge     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bgeu
    movi    a2, 7
    movi    a3, 7
    bgeu    a2, a3, 1f
    test_fail
1:
    movi    a2, 0xffffffff
    bgeu    a2, a3, 1f
    test_fail
1:
    movi    a2, 6
    bgeu    a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bnall
    movi    a2, 0xa5a5a5a5
    movi    a3, 0xa5a5ff00
    bnall   a2, a3, 1f
    test_fail
1:
    movi    a2, 0xa5a5ffa5
    bnall   a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bbs
    movi    a2, 8
    movi    a3, 0xffffff03
    bbs     a2, a3, 1f
    test_fail
1:
    movi    a2, 0xfffffffd
    movi    a3, 0xffffff01
    bbs     a2, a3, 1f
    j       2f
1:
    test_fail
2:
test_end

test bbsi
    movi    a2, 0x00020000
    bbsi    a2, 17, 1f
    test_fail
1:
    movi    a2, 0xfffdffff
    bbsi    a2, 17, 1f
    j       2f
1:
    test_fail
2:
test_end

test_suite_end
