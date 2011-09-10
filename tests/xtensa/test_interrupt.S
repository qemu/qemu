.include "macros.inc"

test_suite interrupt

.macro clear_interrupts
    movi    a2, 0
    wsr     a2, intenable
    wsr     a2, ccompare0
    wsr     a2, ccompare1
    wsr     a2, ccompare2
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

test soft_disabled
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, 0x80
    wsr     a2, intset
    esync
    rsr     a3, interrupt
    assert  eq, a2, a3
    wsr     a2, intclear
    esync
    rsr     a3, interrupt
    assert  eqi, a3, 0
    j       2f
1:
    test_fail
2:
test_end

test soft_intenable
    set_vector kernel, 1f
    clear_interrupts

    movi    a2, 0x80
    wsr     a2, intset
    esync
    rsr     a3, interrupt
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

    movi    a2, 0x80
    wsr     a2, intset
    esync
    rsr     a3, interrupt
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

    movi    a2, 0x80
    wsr     a2, intset
    esync
    rsr     a3, interrupt
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

    movi    a2, 0x80
    wsr     a2, intset
    esync
    rsr     a3, interrupt
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

test soft_priority
    set_vector kernel, 1f
    set_vector level3, 2f
    clear_interrupts

    movi    a2, 0x880
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
    movi    a3, 0x13
    assert  eq, a2, a3      /* EXCM and INTMASK are set
                               for high-priority interrupt */
test_end

test eps_epc_rfi
    set_vector level3, 3f
    clear_interrupts
    reset_ps

    movi    a2, 0x880
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
    rsr     a2, eps3
    assert  eq, a2, a3
    rsr     a2, epc3
    movi    a3, 1b
    assert  ge, a2, a3
    movi    a3, 2b
    assert  ge, a3, a2
    movi    a2, 4f
    wsr     a2, epc3
    movi    a2, 0x40003
    wsr     a2, eps3
    rfi     3
    test_fail
4:
    rsr     a2, ps
    movi    a3, 0x40003
    assert  eq, a2, a3
test_end

test_suite_end
