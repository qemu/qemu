#include "macros.inc"

test_suite rst0

test and
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x01250125
    and     a5, a2, a4
    assert  eq, a5, a6
    and     a2, a2, a4
    assert  eq, a2, a6
    and     a3, a4, a3
    assert  eq, a3, a6
test_end

test or
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xb7ffb7ff
    or      a5, a2, a4
    assert  eq, a5, a6
    or      a2, a2, a4
    assert  eq, a2, a6
    or      a3, a4, a3
    assert  eq, a3, a6
test_end

test xor
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xb6dab6da
    xor     a5, a2, a4
    assert  eq, a5, a6
    xor     a2, a2, a4
    assert  eq, a2, a6
    xor     a3, a4, a3
    assert  eq, a3, a6
test_end

test add
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xb924b924
    add     a5, a2, a4
    assert  eq, a5, a6
    add     a2, a2, a4
    assert  eq, a2, a6
    add     a4, a3, a4
    assert  eq, a4, a6
test_end

test addx2
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xcca45ec9
    addx2   a5, a2, a4
    assert  eq, a5, a6
    addx2   a2, a2, a4
    assert  eq, a2, a6
    addx2   a4, a3, a4
    assert  eq, a4, a6
test_end

test addx4
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xf3a3aa13
    addx4   a5, a2, a4
    assert  eq, a5, a6
    addx4   a2, a2, a4
    assert  eq, a2, a6
    addx4   a4, a3, a4
    assert  eq, a4, a6
test_end

test addx8
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x41a240a7
    addx8   a5, a2, a4
    assert  eq, a5, a6
    addx8   a2, a2, a4
    assert  eq, a2, a6
    addx8   a4, a3, a4
    assert  eq, a4, a6
test_end

test sub
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x6dda9226
    sub     a5, a2, a4
    assert  eq, a5, a6
    sub     a2, a2, a4
    assert  eq, a2, a6
    sub     a4, a3, a4
    assert  eq, a4, a6
test_end

test subx2
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x815a37cb
    subx2   a5, a2, a4
    assert  eq, a5, a6
    subx2   a2, a2, a4
    assert  eq, a2, a6
    subx2   a4, a3, a4
    assert  eq, a4, a6
test_end

test subx4
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xa8598315
    subx4   a5, a2, a4
    assert  eq, a5, a6
    subx4   a2, a2, a4
    assert  eq, a2, a6
    subx4   a4, a3, a4
    assert  eq, a4, a6
test_end

test subx8
    movi    a2, 0x137fa5a5
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0xf65819a9
    subx8   a5, a2, a4
    assert  eq, a5, a6
    subx8   a2, a2, a4
    assert  eq, a2, a6
    subx8   a4, a3, a4
    assert  eq, a4, a6
test_end

test_suite_end
