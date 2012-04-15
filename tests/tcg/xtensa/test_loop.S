.include "macros.inc"

test_suite loop

test loop
    movi    a2, 0
    movi    a3, 5
    loop    a3, 1f
    addi    a2, a2, 1
1:
    assert  eqi, a2, 5
test_end

test loop0
    movi    a2, 0
    loop    a2, 1f
    rsr     a2, lcount
    assert  eqi, a2, -1
    j       1f
1:
test_end

test loop_jump
    movi    a2, 0
    movi    a3, 5
    loop    a3, 1f
    addi    a2, a2, 1
    j       1f
1:
    assert  eqi, a2, 1
test_end

test loop_branch
    movi    a2, 0
    movi    a3, 5
    loop    a3, 1f
    addi    a2, a2, 1
    beqi    a2, 3, 1f
1:
    assert  eqi, a2, 3
test_end

test loop_manual
    movi    a2, 0
    movi    a3, 5
    movi    a4, 1f
    movi    a5, 2f
    wsr     a3, lcount
    wsr     a4, lbeg
    wsr     a5, lend
    isync
    j       1f
.align 4
1:
    addi    a2, a2, 1
2:
    assert  eqi, a2, 6
test_end

test loop_excm
    movi    a2, 0
    movi    a3, 5
    rsr     a4, ps
    movi    a5, 0x10
    or      a4, a4, a5
    wsr     a4, ps
    isync
    loop    a3, 1f
    addi    a2, a2, 1
1:
    xor     a4, a4, a5
    isync
    wsr     a4, ps
    assert  eqi, a2, 1
test_end

test lbeg_invalidation
    movi    a2, 0
    movi    a3, 1
    movi    a4, 1f
    movi    a5, 3f
    wsr     a3, lcount
    wsr     a4, lbeg
    wsr     a5, lend
    isync
    j       1f
.align 4
1:
    addi    a2, a2, 1
    j       2f
.align 4
2:
    addi    a2, a2, 2
    movi    a3, 2b
    wsr     a3, lbeg
    isync
    nop
3:
    assert  eqi, a2, 5
test_end

test lend_invalidation
    movi    a2, 0
    movi    a3, 5
    movi    a4, 1f
    movi    a5, 2f
    wsr     a3, lcount
    wsr     a4, lbeg
    wsr     a5, lend
    isync
    j       1f
.align 4
1:
    addi    a2, a2, 1
2:
    beqi    a3, 3, 1f
    assert  eqi, a2, 6
    movi    a3, 3
    wsr     a3, lcount
    wsr     a4, lend
    isync
    j       1b
1:
    assert  eqi, a2, 7
test_end

test loopnez
    movi    a2, 0
    movi    a3, 5
    loopnez a3, 1f
    addi    a2, a2, 1
1:
    assert  eqi, a2, 5

    movi    a2, 0
    movi    a3, 0
    loopnez a3, 1f
    test_fail
1:
test_end

test loopgtz
    movi    a2, 0
    movi    a3, 5
    loopgtz a3, 1f
    addi    a2, a2, 1
1:
    assert  eqi, a2, 5

    movi    a2, 0
    movi    a3, 0
    loopgtz a3, 1f
    test_fail
1:

    movi    a2, 0
    movi    a3, 0x80000000
    loopgtz a3, 1f
    test_fail
1:
test_end

test_suite_end
