#include "macros.inc"

test_suite load_store

.macro load_ok_test op, type, data, value
    .data
    .align  4
1:
    \type \data
    .previous

    reset_ps
    set_vector kernel, 0
    movi    a3, 1b
    addi    a4, a4, 1
    mov     a5, a4
    \op     a5, a3, 0
    movi    a6, \value
    assert  eq, a5, a6
.endm

#if XCHAL_UNALIGNED_LOAD_EXCEPTION
.macro load_unaligned_test will_trap, op, type, data, value
    .data
    .align  4
    .byte   0
1:
    \type \data
    .previous

    reset_ps
    .ifeq \will_trap
    set_vector kernel, 0
    .else
    set_vector kernel, 2f
    .endif
    movi    a3, 1b
    addi    a4, a4, 1
    mov     a5, a4
1:
    \op     a5, a3, 0
    .ifeq \will_trap
    movi    a6, \value
    assert  eq, a5, a6
    .else
    test_fail
2:
    rsr     a6, exccause
    movi    a7, 9
    assert  eq, a6, a7
    rsr     a6, epc1
    movi    a7, 1b
    assert  eq, a6, a7
    rsr     a6, excvaddr
    assert  eq, a6, a3
    assert  eq, a5, a4
    .endif
    reset_ps
.endm
#else
.macro load_unaligned_test will_trap, op, type, data, value
    .data
    .align  4
1:
    \type \data
    .previous

    reset_ps
    set_vector kernel, 0
    movi    a3, 1b + 1
    addi    a4, a4, 1
    mov     a5, a4
    \op     a5, a3, 0
    movi    a6, \value
    assert  eq, a5, a6
.endm
#endif

.macro store_ok_test op, type, value
    .data
    .align  4
    .byte   0, 0, 0, 0x55
1:
    \type 0
2:
    .byte   0xaa
    .previous

    reset_ps
    set_vector kernel, 0
    movi    a3, 1b
    movi    a5, \value
    \op     a5, a3, 0
    movi    a3, 2b
    l8ui    a5, a3, 0
    movi    a6, 0xaa
    assert  eq, a5, a6
    movi    a3, 1b - 1
    l8ui    a5, a3, 0
    movi    a6, 0x55
    assert  eq, a5, a6
.endm

#if XCHAL_UNALIGNED_STORE_EXCEPTION
.macro store_unaligned_test will_trap, op, nop, type, value
    .data
    .align  4
    .byte   0x55
1:
    \type   0
2:
    .byte   0xaa
    .previous

    reset_ps
    .ifeq \will_trap
    set_vector kernel, 0
    .else
    set_vector kernel, 4f
    .endif
    movi    a3, 1b
    movi    a5, \value
3:
    \op     a5, a3, 0
    .ifne \will_trap
    test_fail
4:
    rsr     a6, exccause
    movi    a7, 9
    assert  eq, a6, a7
    rsr     a6, epc1
    movi    a7, 3b
    assert  eq, a6, a7
    rsr     a6, excvaddr
    assert  eq, a6, a3
    l8ui    a5, a3, 0
    assert  eqi, a5, 0
    .endif
    reset_ps
    movi    a3, 2b
    l8ui    a5, a3, 0
    movi    a6, 0xaa
    assert  eq, a5, a6
    movi    a3, 1b - 1
    l8ui    a5, a3, 0
    movi    a6, 0x55
    assert  eq, a5, a6
.endm
#else
.macro store_unaligned_test will_trap, sop, lop, type, value
    .data
    .align  4
    .byte   0x55
1:
    \type   0
    .previous

    reset_ps
    set_vector kernel, 0
    movi    a3, 1b
    movi    a5, \value
    \sop    a5, a3, 0
    movi    a3, 1b - 1
    \lop    a6, a3, 0
    assert  eq, a5, a6
.endm
#endif

test load_ok
    load_ok_test l16si, .short, 0x00001234, 0x00001234
    load_ok_test l16si, .short, 0x000089ab, 0xffff89ab
    load_ok_test l16ui, .short, 0x00001234, 0x00001234
    load_ok_test l16ui, .short, 0x000089ab, 0x000089ab
    load_ok_test l32i,  .word,  0x12345678, 0x12345678
#if XCHAL_HAVE_RELEASE_SYNC
    load_ok_test l32ai, .word,  0x12345678, 0x12345678
#endif
test_end

#undef WILL_TRAP
#if XCHAL_UNALIGNED_LOAD_HW
#define WILL_TRAP 0
#else
#define WILL_TRAP 1
#endif

test load_unaligned
    load_unaligned_test WILL_TRAP, l16si, .short, 0x00001234, 0x00001234
    load_unaligned_test WILL_TRAP, l16si, .short, 0x000089ab, 0xffff89ab
    load_unaligned_test WILL_TRAP, l16ui, .short, 0x00001234, 0x00001234
    load_unaligned_test WILL_TRAP, l16ui, .short, 0x000089ab, 0x000089ab
    load_unaligned_test WILL_TRAP, l32i,  .word,  0x12345678, 0x12345678
#if XCHAL_HAVE_RELEASE_SYNC
    load_unaligned_test 1,         l32ai, .word,  0x12345678, 0x12345678
#endif
test_end

test store_ok
    store_ok_test s16i,  .short, 0x00001234
    store_ok_test s32i,  .word,  0x12345678
#if XCHAL_HAVE_RELEASE_SYNC
    store_ok_test s32ri, .word,  0x12345678
#endif
test_end

#undef WILL_TRAP
#if XCHAL_UNALIGNED_STORE_HW
#define WILL_TRAP 0
#else
#define WILL_TRAP 1
#endif

test store_unaligned
    store_unaligned_test WILL_TRAP, s16i,  l16ui, .short, 0x00001234
    store_unaligned_test WILL_TRAP, s32i,  l32i,  .word,  0x12345678
#if XCHAL_HAVE_RELEASE_SYNC
    store_unaligned_test 1,         s32ri, l32i,  .word,  0x12345678
#endif
test_end

test_suite_end
