.include "macros.inc"

test_suite boolean

test all4
    movi    a2, 0xfec0
    wsr     a2, br
    all4    b0, b0
    rsr     a3, br
    assert  eq, a2, a3
    all4    b0, b4
    rsr     a3, br
    assert  eq, a2, a3
    all4    b0, b8
    rsr     a3, br
    assert  eq, a2, a3
    all4    b0, b12
    rsr     a3, br
    addi    a2, a2, 1
    assert  eq, a2, a3
test_end

test_suite_end
