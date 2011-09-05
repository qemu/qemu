.include "macros.inc"

test_suite timer

test ccount
    rsr     a3, ccount
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 1
test_end

test ccompare
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    wsr     a2, ccompare1
    wsr     a2, ccompare2

    movi    a3, 20
    rsr     a2, ccount
    addi    a2, a2, 20
    wsr     a2, ccompare0
    rsr     a2, interrupt
    assert  eqi, a2, 0
    loop    a3, 1f
    rsr     a3, interrupt
    bnez    a3, 2f
1:
    test_fail
2:
test_end

test ccompare0_interrupt
    set_vector kernel, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    wsr     a2, ccompare1
    wsr     a2, ccompare2

    movi    a3, 20
    rsr     a2, ccount
    addi    a2, a2, 20
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 0x40
    wsr     a2, intenable
    rsil    a2, 0
    loop    a3, 1f
    nop
1:
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
test_end

test ccompare1_interrupt
    set_vector level3, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    wsr     a2, ccompare0
    wsr     a2, ccompare2

    movi    a3, 20
    rsr     a2, ccount
    addi    a2, a2, 20
    wsr     a2, ccompare1
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0
    movi    a2, 0x400
    wsr     a2, intenable
    rsil    a2, 2
    loop    a3, 1f
    nop
1:
    test_fail
2:
test_end

test ccompare2_interrupt
    set_vector level5, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    wsr     a2, ccompare0
    wsr     a2, ccompare1

    movi    a3, 20
    rsr     a2, ccount
    addi    a2, a2, 20
    wsr     a2, ccompare2
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0
    movi    a2, 0x2000
    wsr     a2, intenable
    rsil    a2, 4
    loop    a3, 1f
    nop
1:
    test_fail
2:
test_end

test_suite_end
