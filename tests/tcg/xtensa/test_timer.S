#include "macros.inc"

#define CCOUNT_SHIFT 4
#define WAIT_LOOPS 20

.macro      make_ccount_delta target, delta
    rsr     \delta, ccount
    rsr     \target, ccount
    sub     \delta, \target, \delta
    slli    \delta, \delta, CCOUNT_SHIFT
    add     \target, \target, \delta
.endm

test_suite timer

test ccount
    rsr     a3, ccount
    rsr     a4, ccount
    assert  ne, a3, a4
test_end

test ccount_write
    rsr     a3, ccount
    rsr     a4, ccount
    sub     a4, a4, a3
    movi    a2, 0x12345678
    wsr     a2, ccount
    esync
    rsr     a3, ccount
    sub     a3, a3, a2
    slli    a4, a4, 2
    assert  ltu, a3, a4
test_end

test ccount_update_deadline
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare1
    wsr     a2, ccompare2
    movi    a2, 0x12345678
    wsr     a2, ccompare0
    rsr     a3, interrupt
    assert  eqi, a3, 0
    movi    a2, 0x12345677
    wsr     a2, ccount
    esync
    nop
    rsr     a2, interrupt
    movi    a3, 1 << XCHAL_TIMER0_INTERRUPT
    assert  eq, a2, a3
test_end

test ccompare
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare1
    wsr     a2, ccompare2

    make_ccount_delta a2, a15
    wsr     a2, ccompare0
1:
    rsr     a3, interrupt
    rsr     a4, ccount
    rsr     a5, interrupt
    sub     a4, a4, a2
    bgez    a4, 2f
    assert  eqi, a3, 0
    j       1b
2:
    assert  nei, a5, 0
test_end

test ccompare0_interrupt
    set_vector kernel, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare1
    wsr     a2, ccompare2

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 1 << XCHAL_TIMER0_INTERRUPT
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
    movi    a2, 0
    wsr     a2, ccompare0
    wsr     a2, ccompare2

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare1
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0
    movi    a2, 1 << XCHAL_TIMER1_INTERRUPT
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
    movi    a2, 0
    wsr     a2, ccompare0
    wsr     a2, ccompare1

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare2
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0
    movi    a2, 1 << XCHAL_TIMER2_INTERRUPT
    wsr     a2, intenable
    rsil    a2, 4
    loop    a3, 1f
    nop
1:
    test_fail
2:
test_end

test ccompare_interrupt_masked
    set_vector kernel, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare2

    movi    a3, 2 * WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare1
    add     a2, a2, a15
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 1 << XCHAL_TIMER0_INTERRUPT
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

test ccompare_interrupt_masked_waiti
    set_vector kernel, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare2

    movi    a3, 2 * WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare1
    add     a2, a2, a15
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 1 << XCHAL_TIMER0_INTERRUPT
    wsr     a2, intenable
    waiti   0
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
test_end

test_suite_end
