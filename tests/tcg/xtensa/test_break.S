#include "macros.inc"

#define debug_level 6
#define debug_vector level6

test_suite break

test break
    set_vector debug_vector, 0
    rsil    a2, debug_level
    _break  0, 0

    set_vector debug_vector, 2f
    rsil    a2, debug_level - 1
1:
    _break  0, 0
    test_fail
2:
    rsr     a2, ps
    movi    a3, 0x1f
    and     a2, a2, a3
    movi    a3, 0x10 | debug_level
    assert  eq, a2, a3
    rsr     a2, epc6
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x8
    assert  eq, a2, a3
test_end

test breakn
    set_vector debug_vector, 0
    rsil    a2, debug_level
    _break.n  0

    set_vector debug_vector, 2f
    rsil    a2, debug_level - 1
1:
    _break.n  0
    test_fail
2:
    rsr     a2, ps
    movi    a3, 0x1f
    and     a2, a2, a3
    movi    a3, 0x10 | debug_level
    assert  eq, a2, a3
    rsr     a2, epc6
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x10
    assert  eq, a2, a3
test_end

test ibreak
    set_vector debug_vector, 0
    rsil    a2, debug_level
    movi    a2, 1f
    wsr     a2, ibreaka0
    movi    a2, 1
    wsr     a2, ibreakenable
    isync
1:
    rsil    a2, debug_level - 1
    movi    a2, 1f
    wsr     a2, ibreaka0
    movi    a2, 0
    wsr     a2, ibreakenable
    isync
1:
    set_vector debug_vector, 2f
    movi    a2, 1f
    wsr     a2, ibreaka0
    movi    a2, 1
    wsr     a2, ibreakenable
    isync
1:
    test_fail
2:
    rsr     a2, ps
    movi    a3, 0x1f
    and     a2, a2, a3
    movi    a3, 0x10 | debug_level
    assert  eq, a2, a3
    rsr     a2, epc6
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x2
    assert  eq, a2, a3
test_end

test ibreak_remove
    set_vector debug_vector, 3f
    rsil    a2, debug_level - 1
    movi    a2, 2f
    wsr     a2, ibreaka0
    movi    a3, 1
1:
    wsr     a3, ibreakenable
    isync
2:
    beqz    a3, 4f
    test_fail
3:
    assert  eqi, a3, 1
    rsr     a2, ps
    movi    a3, 0x1f
    and     a2, a2, a3
    movi    a3, 0x10 | debug_level
    assert  eq, a2, a3
    rsr     a2, epc6
    movi    a3, 2b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x2
    assert  eq, a2, a3

    movi    a2, 0x40000
    wsr     a2, ps
    isync
    movi    a3, 0
    j       1b
4:
test_end

test ibreak_priority
    set_vector debug_vector, 2f
    rsil    a2, debug_level - 1
    movi    a2, 1f
    wsr     a2, ibreaka0
    movi    a2, 1
    wsr     a2, ibreakenable
    isync
1:
    break   0, 0
    test_fail
2:
    rsr     a2, debugcause
    movi    a3, 0x2
    assert  eq, a2, a3
test_end

test icount
    set_vector debug_vector, 2f
    rsil    a2, debug_level - 1
    movi    a2, -2
    wsr     a2, icount
    movi    a2, 1
    wsr     a2, icountlevel
    isync
    rsil    a2, 0
    nop
1:
    break   0, 0
    test_fail
2:
    movi    a2, 0
    wsr     a2, icountlevel
    rsr     a2, epc6
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x1
    assert  eq, a2, a3
test_end

.macro check_dbreak dr
    rsr     a2, epc6
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, debugcause
    movi    a3, 0x4 | (\dr << 8)
    assert  eq, a2, a3
    movi    a2, 0
    wsr     a2, dbreakc\dr
.endm

.macro dbreak_test dr, ctl, break, access, op
    set_vector debug_vector, 2f
    rsil    a2, debug_level - 1
    movi    a2, \ctl
    wsr     a2, dbreakc\dr
    movi    a2, \break
    wsr     a2, dbreaka\dr
    movi    a2, \access
    isync
1:
    \op     a3, a2, 0
    test_fail
2:
    check_dbreak \dr
    reset_ps
.endm

test dbreak_exact
    dbreak_test 0, 0x4000003f, 0xd000007f, 0xd000007f, l8ui
    dbreak_test 1, 0x4000003e, 0xd000007e, 0xd000007e, l16ui
    dbreak_test 0, 0x4000003c, 0xd000007c, 0xd000007c, l32i

    dbreak_test 1, 0x8000003f, 0xd000007f, 0xd000007f, s8i
    dbreak_test 0, 0x8000003e, 0xd000007e, 0xd000007e, s16i
    dbreak_test 1, 0x8000003c, 0xd000007c, 0xd000007c, s32i
test_end

test dbreak_overlap
    dbreak_test 0, 0x4000003f, 0xd000007d, 0xd000007c, l16ui
    dbreak_test 1, 0x4000003f, 0xd000007d, 0xd000007c, l32i

    dbreak_test 0, 0x4000003e, 0xd000007e, 0xd000007f, l8ui
    dbreak_test 1, 0x4000003e, 0xd000007e, 0xd000007c, l32i

    dbreak_test 0, 0x4000003c, 0xd000007c, 0xd000007d, l8ui
    dbreak_test 1, 0x4000003c, 0xd000007c, 0xd000007c, l16ui

    dbreak_test 0, 0x40000038, 0xd0000078, 0xd000007b, l8ui
    dbreak_test 1, 0x40000038, 0xd0000078, 0xd000007a, l16ui
    dbreak_test 0, 0x40000038, 0xd0000078, 0xd000007c, l32i

    dbreak_test 1, 0x40000030, 0xd0000070, 0xd0000075, l8ui
    dbreak_test 0, 0x40000030, 0xd0000070, 0xd0000076, l16ui
    dbreak_test 1, 0x40000030, 0xd0000070, 0xd0000078, l32i

    dbreak_test 0, 0x40000020, 0xd0000060, 0xd000006f, l8ui
    dbreak_test 1, 0x40000020, 0xd0000060, 0xd0000070, l16ui
    dbreak_test 0, 0x40000020, 0xd0000060, 0xd0000074, l32i


    dbreak_test 0, 0x8000003f, 0xd000007d, 0xd000007c, s16i
    dbreak_test 1, 0x8000003f, 0xd000007d, 0xd000007c, s32i

    dbreak_test 0, 0x8000003e, 0xd000007e, 0xd000007f, s8i
    dbreak_test 1, 0x8000003e, 0xd000007e, 0xd000007c, s32i

    dbreak_test 0, 0x8000003c, 0xd000007c, 0xd000007d, s8i
    dbreak_test 1, 0x8000003c, 0xd000007c, 0xd000007c, s16i

    dbreak_test 0, 0x80000038, 0xd0000078, 0xd000007b, s8i
    dbreak_test 1, 0x80000038, 0xd0000078, 0xd000007a, s16i
    dbreak_test 0, 0x80000038, 0xd0000078, 0xd000007c, s32i

    dbreak_test 1, 0x80000030, 0xd0000070, 0xd0000075, s8i
    dbreak_test 0, 0x80000030, 0xd0000070, 0xd0000076, s16i
    dbreak_test 1, 0x80000030, 0xd0000070, 0xd0000078, s32i

    dbreak_test 0, 0x80000020, 0xd0000060, 0xd000006f, s8i
    dbreak_test 1, 0x80000020, 0xd0000060, 0xd0000070, s16i
    dbreak_test 0, 0x80000020, 0xd0000060, 0xd0000074, s32i
test_end

test dbreak_invalid
    dbreak_test 0, 0x40000030, 0xd0000071, 0xd0000070, l16ui
    dbreak_test 1, 0x40000035, 0xd0000072, 0xd0000070, l32i
test_end

test_suite_end
