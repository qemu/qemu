#include "macros.inc"

test_suite rem

#if XCHAL_HAVE_DIV32

test remu_pp
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x0c5caa17
    remu    a5, a2, a4
    assert  eq, a5, a6
    remu    a2, a2, a4
    assert  eq, a2, a6
    remu    a4, a3, a4
    assert  eq, a4, a6
test_end

test remu_np
    movi    a2, 0xa5a5137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x9aa40af
    remu    a5, a2, a4
    assert  eq, a5, a6
    remu    a2, a2, a4
    assert  eq, a2, a6
    remu    a4, a3, a4
    assert  eq, a4, a6
test_end

test remu_pn
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0xf7315a5a
    movi    a6, 0x5a5a137f
    remu    a5, a2, a4
    assert  eq, a5, a6
    remu    a2, a2, a4
    assert  eq, a2, a6
    remu    a4, a3, a4
    assert  eq, a4, a6
test_end

test remu_nn
    movi    a2, 0xf7315a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x518c46db
    remu    a5, a2, a4
    assert  eq, a5, a6
    remu    a2, a2, a4
    assert  eq, a2, a6
    remu    a4, a3, a4
    assert  eq, a4, a6
test_end

test remu_exc
    set_vector kernel, 2f
    movi    a2, 0xf7315a5a
    movi    a4, 0x00000000
1:
    remu    a5, a2, a4
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 6 /* INTEGER_DIVIDE_BY_ZERO_CAUSE */
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
test_end

test rems_pp
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0x0c5caa17
    rems    a5, a2, a4
    assert  eq, a5, a6
    rems    a2, a2, a4
    assert  eq, a2, a6
    rems    a4, a3, a4
    assert  eq, a4, a6
test_end

test rems_np
    movi    a2, 0xa5a5137f
    mov     a3, a2
    movi    a4, 0x137f5a5a
    movi    a6, 0xf3a27ce7
    rems    a5, a2, a4
    assert  eq, a5, a6
    rems    a2, a2, a4
    assert  eq, a2, a6
    rems    a4, a3, a4
    assert  eq, a4, a6
test_end

test rems_pn
    movi    a2, 0x5a5a137f
    mov     a3, a2
    movi    a4, 0xf7315a5a
    movi    a6, 0x02479b03
    rems    a5, a2, a4
    assert  eq, a5, a6
    rems    a2, a2, a4
    assert  eq, a2, a6
    rems    a4, a3, a4
    assert  eq, a4, a6
test_end

test rems_nn
    movi    a2, 0xf7315a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xf7315a5a
    rems    a5, a2, a4
    assert  eq, a5, a6
    rems    a2, a2, a4
    assert  eq, a2, a6
    rems    a4, a3, a4
    assert  eq, a4, a6
test_end

test rems_over
    movi    a2, 0x80000000
    movi    a4, 0xffffffff
    movi    a6, 0
    rems    a5, a2, a4
    assert  eq, a5, a6
test_end

test rems_exc
    set_vector kernel, 2f
    movi    a2, 0xf7315a5a
    movi    a4, 0x00000000
1:
    rems    a5, a2, a4
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 6 /* INTEGER_DIVIDE_BY_ZERO_CAUSE */
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
test_end

#endif

test_suite_end
