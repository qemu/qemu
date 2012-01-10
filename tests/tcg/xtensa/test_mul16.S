.include "macros.inc"

test_suite mul16

test mul16u_pp
    movi    a2, 0x137f5a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x06e180a6
    mul16u  a5, a2, a4
    assert  eq, a5, a6
    mul16u  a2, a2, a4
    assert  eq, a2, a6
    mul16u  a3, a4, a3
    assert  eq, a3, a6
test_end

test mul16u_np
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x0c9d6bdb
    mul16u  a5, a2, a4
    assert  eq, a5, a6
    mul16u  a2, a2, a4
    assert  eq, a2, a6
    mul16u  a3, a4, a3
    assert  eq, a3, a6
test_end

test mul16u_nn
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5f731
    movi    a6, 0x9ff1e795
    mul16u  a5, a2, a4
    assert  eq, a5, a6
    mul16u  a2, a2, a4
    assert  eq, a2, a6
    mul16u  a3, a4, a3
    assert  eq, a3, a6
test_end

test mul16s_pp
    movi    a2, 0x137f5a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x06e180a6
    mul16s  a5, a2, a4
    assert  eq, a5, a6
    mul16s  a2, a2, a4
    assert  eq, a2, a6
    mul16s  a3, a4, a3
    assert  eq, a3, a6
test_end

test mul16s_np
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xf91e6bdb
    mul16s  a5, a2, a4
    assert  eq, a5, a6
    mul16s  a2, a2, a4
    assert  eq, a2, a6
    mul16s  a3, a4, a3
    assert  eq, a3, a6
test_end

test mul16s_nn
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5f731
    movi    a6, 0x031be795
    mul16s  a5, a2, a4
    assert  eq, a5, a6
    mul16s  a2, a2, a4
    assert  eq, a2, a6
    mul16s  a3, a4, a3
    assert  eq, a3, a6
test_end

test_suite_end
