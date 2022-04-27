#include "macros.inc"

#define CCOUNT_SHIFT 4
#define WAIT_LOOPS 20
#define level1 kernel
#define INTERRUPT_LEVEL(n) glue3(XCHAL_INT, n, _LEVEL)

.macro      make_ccount_delta target, delta
    rsr     \delta, ccount
    rsr     \target, ccount
    sub     \delta, \target, \delta
    slli    \delta, \delta, CCOUNT_SHIFT
    add     \target, \target, \delta
.endm

test_suite timer

#if XCHAL_HAVE_CCOUNT

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

#if XCHAL_NUM_TIMERS

#if INTERRUPT_LEVEL(XCHAL_TIMER0_INTERRUPT) == 1
#define TIMER0_VECTOR kernel
#else
#define TIMER0_VECTOR glue(level, INTERRUPT_LEVEL(XCHAL_TIMER0_INTERRUPT))
#endif

#if XCHAL_NUM_TIMERS > 1
#if INTERRUPT_LEVEL(XCHAL_TIMER1_INTERRUPT) == 1
#define TIMER1_VECTOR kernel
#else
#define TIMER1_VECTOR glue(level, INTERRUPT_LEVEL(XCHAL_TIMER1_INTERRUPT))
#endif
#endif

#if XCHAL_NUM_TIMERS > 2
#if INTERRUPT_LEVEL(XCHAL_TIMER2_INTERRUPT) == 1
#define TIMER2_VECTOR kernel
#else
#define TIMER2_VECTOR glue(level, INTERRUPT_LEVEL(XCHAL_TIMER2_INTERRUPT))
#endif
#endif

test ccount_update_deadline
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif
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
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif

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
    set_vector TIMER0_VECTOR, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 1 << XCHAL_TIMER0_INTERRUPT
    wsr     a2, intenable
    rsil    a2, 0
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    test_fail
2:
#if INTERRUPT_LEVEL(XCHAL_TIMER0_INTERRUPT) == 1
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
#endif
test_end

#if XCHAL_NUM_TIMERS > 1

test ccompare1_interrupt
    set_vector TIMER1_VECTOR, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
    wsr     a2, ccompare0
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
    wsr     a2, ccompare1
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0
    movi    a2, 1 << XCHAL_TIMER1_INTERRUPT
    wsr     a2, intenable
    rsil    a2, INTERRUPT_LEVEL(XCHAL_TIMER1_INTERRUPT) - 1
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    test_fail
2:
#if INTERRUPT_LEVEL(XCHAL_TIMER1_INTERRUPT) == 1
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
#endif
test_end

#endif
#if XCHAL_NUM_TIMERS > 2

test ccompare2_interrupt
    set_vector TIMER2_VECTOR, 2f
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
    rsil    a2, INTERRUPT_LEVEL(XCHAL_TIMER2_INTERRUPT) - 1
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    test_fail
2:
#if INTERRUPT_LEVEL(XCHAL_TIMER2_INTERRUPT) == 1
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
#endif
test_end

#endif

test ccompare_interrupt_masked
    set_vector TIMER0_VECTOR, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif

    movi    a3, WAIT_LOOPS
    make_ccount_delta a2, a15
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
    add     a2, a2, a15
    wsr     a2, ccompare0
    rsync
    rsr     a2, interrupt
    assert  eqi, a2, 0

    movi    a2, 1 << XCHAL_TIMER0_INTERRUPT
    wsr     a2, intenable
    rsil    a2, 0
1:
    addi    a3, a3, -1
    bnez    a3, 1b

    test_fail
2:
#if INTERRUPT_LEVEL(XCHAL_TIMER0_INTERRUPT) == 1
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
#endif
test_end

test ccompare_interrupt_masked_waiti
    set_vector TIMER0_VECTOR, 2f
    movi    a2, 0
    wsr     a2, intenable
    rsr     a2, interrupt
    wsr     a2, intclear
    movi    a2, 0
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif

    make_ccount_delta a2, a15
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
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
#if INTERRUPT_LEVEL(XCHAL_TIMER0_INTERRUPT) == 1
    rsr     a2, exccause
    assert  eqi, a2, 4 /* LEVEL1_INTERRUPT_CAUSE */
#endif
test_end

#endif
#endif

test_suite_end
