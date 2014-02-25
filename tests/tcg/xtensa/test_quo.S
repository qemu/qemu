#include "macros.inc"

test_suite quo

test quou_pp
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x4
    quou    a5, a2, a4
    assert  eq, a5, a6
    quou    a2, a2, a4
    assert  eq, a2, a6
    quou    a4, a3, a4
    assert  eq, a4, a6
test_end

test quou_np
    movi    a2, 0xa5a5137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x8
    quou    a5, a2, a4
    assert  eq, a5, a6
    quou    a2, a2, a4
    assert  eq, a2, a6
    quou    a4, a3, a4
    assert  eq, a4, a6
test_end

test quou_pn
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0xf7315a5a
    movi    a6, 0
    quou    a5, a2, a4
    assert  eq, a5, a6
    quou    a2, a2, a4
    assert  eq, a2, a6
    quou    a4, a3, a4
    assert  eq, a4, a6
test_end

test quou_nn
    movi    a2, 0xf7315a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x1
    quou    a5, a2, a4
    assert  eq, a5, a6
    quou    a2, a2, a4
    assert  eq, a2, a6
    quou    a4, a3, a4
    assert  eq, a4, a6
test_end

test quou_exc
    set_vector kernel, 2f
    movi    a2, 0xf7315a5a
    movi    a4, 0x00000000
1:
    quou    a5, a2, a4
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 6 /* INTEGER_DIVIDE_BY_ZERO_CAUSE */
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
test_end

test quos_pp
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x4
    quos    a5, a2, a4
    assert  eq, a5, a6
    quos    a2, a2, a4
    assert  eq, a2, a6
    quos    a4, a3, a4
    assert  eq, a4, a6
test_end

test quos_np
    movi    a2, 0xa5a5137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0xfffffffc
    quos    a5, a2, a4
    assert  eq, a5, a6
    quos    a2, a2, a4
    assert  eq, a2, a6
    quos    a4, a3, a4
    assert  eq, a4, a6
test_end

test quos_pn
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0xf7315a5a
    movi    a6, 0xfffffff6
    quos    a5, a2, a4
    assert  eq, a5, a6
    quos    a2, a2, a4
    assert  eq, a2, a6
    quos    a4, a3, a4
    assert  eq, a4, a6
test_end

test quos_nn
    movi    a2, 0xf7315a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0
    quos    a5, a2, a4
    assert  eq, a5, a6
    quos    a2, a2, a4
    assert  eq, a2, a6
    quos    a4, a3, a4
    assert  eq, a4, a6
test_end

test quos_over
    movi    a2, 0x80000000
    movi    a4, 0xffffffff
    movi    a6, 0x80000000
    quos    a5, a2, a4
    assert  eq, a5, a6
test_end

test quos_exc
    set_vector kernel, 2f
    movi    a2, 0xf7315a5a
    movi    a4, 0x00000000
1:
    quos    a5, a2, a4
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 6 /* INTEGER_DIVIDE_BY_ZERO_CAUSE */
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
test_end

test_suite_end
