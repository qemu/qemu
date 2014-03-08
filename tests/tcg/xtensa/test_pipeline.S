#include "macros.inc"

.purgem test
.macro test name
    movi    a2, 1f
    movi    a3, 99f
0:
    ipf     a2, 0
    ipf     a2, 4
    ipf     a2, 8
    ipf     a2, 12
    addi    a2, a2, 16
    blt     a2, a3, 0b
    j       1f
    .align 4
1:
.endm

test_suite pipeline

test register_no_stall
    rsr     a3, ccount
    add     a5, a6, a6
    add     a6, a5, a5
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 3
test_end

test register_stall
    l32i    a5, a1, 0   /* data cache preload */
    nop
    rsr     a3, ccount
    l32i    a5, a1, 0
    add     a6, a5, a5  /* M-to-E interlock */
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

test j0_stall
    rsr     a3, ccount
    j       1f          /* E + 2-cycle penalty */
1:
    rsr     a4, ccount  /* E */
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

test j1_stall
    rsr     a3, ccount
    j       1f
    nop
1:
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

test j5_stall
    rsr     a3, ccount
    j       1f
    nop
    nop
    nop
    nop
    nop
1:
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

test b_no_stall
    movi    a5, 1
    rsr     a3, ccount
    beqi    a5, 2, 1f
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 2
1:
test_end

test b1_stall
    movi    a5, 1
    rsr     a3, ccount
    beqi    a5, 1, 1f
    nop
1:
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

test b5_stall
    movi    a5, 1
    rsr     a3, ccount
    beqi    a5, 1, 1f
    nop
    nop
    nop
    nop
    nop
1:
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 4
test_end

/* PS *SYNC */

test ps_dsync
    rsr     a5, ps
    isync
    rsr     a3, ccount
    wsr     a5, ps
    dsync
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 5
test_end

test ps_esync
    rsr     a5, ps
    isync
    rsr     a3, ccount
    wsr     a5, ps
    esync
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 5
test_end

test ps_rsync
    rsr     a5, ps
    isync
    rsr     a3, ccount
    wsr     a5, ps
    rsync
    rsr     a4, ccount
    sub     a3, a4, a3
    assert  eqi, a3, 5
test_end

test ps_isync
    rsr     a5, ps
    isync
    rsr     a3, ccount
    wsr     a5, ps
    isync
    rsr     a4, ccount
    sub     a3, a4, a3
    movi    a4, 9
    assert  eq, a3, a4
test_end

test_suite_end
