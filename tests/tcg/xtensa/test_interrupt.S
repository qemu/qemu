#include "macros.inc"

#define LSBIT(v) ((v) & -(v))

#define LEVEL_MASK(x) glue3(XCHAL_INTLEVEL, x, _MASK)
#define LEVEL_SOFT_MASK(x) (LEVEL_MASK(x) & XCHAL_INTTYPE_MASK_SOFTWARE)

#define L1_SOFT_MASK LEVEL_SOFT_MASK(1)
#define L1_SOFT LSBIT(L1_SOFT_MASK)

#if LEVEL_SOFT_MASK(2)
#define HIGH_LEVEL_SOFT_MASK LEVEL_SOFT_MASK(2)
#elif LEVEL_SOFT_MASK(3)
#define HIGH_LEVEL_SOFT_MASK LEVEL_SOFT_MASK(3)
#elif LEVEL_SOFT_MASK(4)
#define HIGH_LEVEL_SOFT_MASK LEVEL_SOFT_MASK(4)
#elif LEVEL_SOFT_MASK(5)
#define HIGH_LEVEL_SOFT_MASK LEVEL_SOFT_MASK(5)
#elif LEVEL_SOFT_MASK(6)
#define HIGH_LEVEL_SOFT_MASK LEVEL_SOFT_MASK(6)
#else
#define HIGH_LEVEL_SOFT_MASK 0
#endif

#define HIGH_LEVEL_SOFT LSBIT(HIGH_LEVEL_SOFT_MASK)

#if LEVEL_SOFT_MASK(2)
#define HIGH_LEVEL_SOFT_LEVEL 2
#elif LEVEL_SOFT_MASK(3)
#define HIGH_LEVEL_SOFT_LEVEL 3
#elif LEVEL_SOFT_MASK(4)
#define HIGH_LEVEL_SOFT_LEVEL 4
#elif LEVEL_SOFT_MASK(5)
#define HIGH_LEVEL_SOFT_LEVEL 5
#elif LEVEL_SOFT_MASK(6)
#define HIGH_LEVEL_SOFT_LEVEL 6
#else
#define HIGH_LEVEL_SOFT_LEVEL 0
#endif

test_suite interrupt

#if XCHAL_HAVE_INTERRUPTS

.macro clear_interrupts
    movi    a2, 0
    wsr     a2, intenable
#if XCHAL_NUM_TIMERS
    wsr     a2, ccompare0
#endif
#if XCHAL_NUM_TIMERS > 1
    wsr     a2, ccompare1
#endif
#if XCHAL_NUM_TIMERS > 2
    wsr     a2, ccompare2
#endif
    esync
    rsr     a2, interrupt
    wsr     a2, intclear

    esync
    rsr     a2, interrupt
    assert  eqi, a2, 0
.endm

.macro check_l1
    rsr     a2, ps
    movi    a3, 0x1f        /* EXCM | INTMASK */
    and     a2, a2, a3
    assert  eqi, a2, 0x10   /* only EXCM is set for level-1 interrupt */
    rsr     a2, exccause
    assert  eqi, a2, 4
.endm

test rsil
    clear_interrupts

    rsr     a2, ps
    rsil    a3, 7
    rsr     a4, ps
    assert  eq, a2, a3
    movi    a2, 0xf
    and     a2, a4, a2
    assert  eqi, a2, 7
    xor     a3, a3, a4
    movi    a2, 0xfffffff0
    and     a2, a3, a2
    assert  eqi, a2, 0
test_end

#if L1_SOFT
test soft_disabled
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, L1_SOFT
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    movi    a4, ~XCHAL_INTTYPE_MASK_TIMER
    and     a3, a3, a4
    assert  eq, a2, a3
    wsr     a2, intclear
    esync
    rsr     a3, interrupt
    and     a3, a3, a4
    assert  eqi, a3, 0
    j       2f
1:
    test_fail
2:
test_end

test soft_intenable
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, L1_SOFT
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    movi    a4, ~XCHAL_INTTYPE_MASK_TIMER
    and     a3, a3, a4
    assert  eq, a2, a3
    rsil    a3, 0
    wsr     a2, intenable
    esync
    test_fail
1:
    check_l1
test_end

test soft_rsil
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, L1_SOFT
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    movi    a4, ~XCHAL_INTTYPE_MASK_TIMER
    and     a3, a3, a4
    assert  eq, a2, a3
    wsr     a2, intenable
    rsil    a3, 0
    esync
    test_fail
1:
    check_l1
test_end

test soft_waiti
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, L1_SOFT
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    movi    a4, ~XCHAL_INTTYPE_MASK_TIMER
    and     a3, a3, a4
    assert  eq, a2, a3
    wsr     a2, intenable
    waiti   0
    test_fail
1:
    check_l1
test_end

test soft_user
    set_vector kernel, 1f
    set_vector user, 2f
    clear_interrupts

    movi    a2, L1_SOFT
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    movi    a4, ~XCHAL_INTTYPE_MASK_TIMER
    and     a3, a3, a4
    assert  eq, a2, a3
    wsr     a2, intenable

    rsr     a2, ps
    movi    a3, 0x20
    or      a2, a2, a3
    wsr     a2, ps
    waiti   0
1:
    test_fail
2:
    check_l1
test_end

#if HIGH_LEVEL_SOFT
test soft_priority
    set_vector kernel, 1f
    set_vector glue(level, HIGH_LEVEL_SOFT_LEVEL), 2f
    clear_interrupts

    movi    a2, L1_SOFT | HIGH_LEVEL_SOFT
    wsr     a2, intenable
    rsil    a3, 0
    esync
    wsr     a2, intset
    esync
1:
    test_fail
2:
    rsr     a2, ps
    movi    a3, 0x1f        /* EXCM | INTMASK */
    and     a2, a2, a3
    movi    a3, 0x10 | HIGH_LEVEL_SOFT_LEVEL
    assert  eq, a2, a3      /* EXCM and INTMASK are set
                               for high-priority interrupt */
test_end
#endif
#endif

#if HIGH_LEVEL_SOFT
test eps_epc_rfi
    set_vector glue(level, HIGH_LEVEL_SOFT_LEVEL), 3f
    clear_interrupts
    reset_ps

    movi    a2, L1_SOFT_MASK | HIGH_LEVEL_SOFT_MASK
    wsr     a2, intenable
    rsil    a3, 0
    rsr     a3, ps
    esync
    wsr     a2, intset
1:
    esync
2:
    test_fail
3:
    rsr     a2, glue(eps, HIGH_LEVEL_SOFT_LEVEL)
    assert  eq, a2, a3
    rsr     a2, glue(epc, HIGH_LEVEL_SOFT_LEVEL)
    movi    a3, 1b
    assert  ge, a2, a3
    movi    a3, 2b
    assert  ge, a3, a2
    movi    a2, 4f
    wsr     a2, glue(epc, HIGH_LEVEL_SOFT_LEVEL)
    movi    a2, 0x40000 | HIGH_LEVEL_SOFT_LEVEL
    wsr     a2, glue(eps, HIGH_LEVEL_SOFT_LEVEL)
    rfi     HIGH_LEVEL_SOFT_LEVEL
    test_fail
4:
    rsr     a2, ps
    movi    a3, 0x40000 | HIGH_LEVEL_SOFT_LEVEL
    assert  eq, a2, a3
test_end
#endif

#endif

test_suite_end
