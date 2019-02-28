#include "macros.inc"

test_suite windowed

#if XCHAL_HAVE_WINDOWED

.altmacro

.macro reset_window start
    movi    a2, 0xffff
    wsr     a2, windowstart
    rsync
    movi    a2, 0
    wsr     a2, windowbase
    rsync
    movi    a2, \start
    wsr     a2, windowstart
    rsync
.endm

.macro overflow_test shift, window, probe_ok, probe_ex
    set_vector window_overflow_4, 0
    set_vector window_overflow_8, 0
    set_vector window_overflow_12, 0

    movi    a2, 1 | (((1 << ((\window) / 4)) | 1) << ((\shift) / 4))
    wsr     a2, windowstart
    reset_ps

    mov     a2, a\probe_ok
    set_vector window_overflow_\window, 10f
1:
    mov     a2, a\probe_ex
    test_fail
10:
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    movi    a2, 2f
    wsr     a2, epc1

    rsr     a2, windowbase
    movi    a3, (\shift) / 4
    assert  eq, a2, a3
    rsr     a2, ps
    movi    a3, 0x4001f
    assert  eq, a2, a3
    rfwo
    test_fail
2:
    rsr     a2, windowbase
    assert  eqi, a2, 0
    rsr     a2, windowstart
    movi    a3, 1 | ((1 << ((\window) / 4)) << ((\shift) / 4))
    assert  eq, a2, a3
    rsr     a2, ps
    movi    a3, 0x4000f
    assert  eq, a2, a3
.endm

.macro overflow_tests shift, window, probe
    .if \probe < 15
    overflow_test \shift, \window, %((\shift) - 1), \probe
    overflow_tests \shift, \window, %((\probe) + 1)
    .endif
.endm

.macro all_overflow_tests
    .irp shift, 4, 8, 12
    .irp window, 4, 8, 12
    overflow_tests \shift, \window, \shift
    .endr
    .endr
.endm

test overflow
    all_overflow_tests
test_end


.macro underflow_test window
    set_vector window_underflow_4, 0
    set_vector window_underflow_8, 0
    set_vector window_underflow_12, 0

    set_vector window_underflow_\window, 10f

    reset_window 1
    reset_ps

    ssai    2
    movi    a2, 2f
    slli    a2, a2, 2
    movi    a3, (\window) / 4
    src     a0, a3, a2
1:
    retw
    test_fail
10:
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    movi    a2, 2f
    wsr     a2, epc1

    rsr     a2, ps
    movi    a3, 0x4001f
    assert  eq, a2, a3
    rsr     a2, windowbase
    movi    a3, (XCHAL_NUM_AREGS - (\window)) / 4
    assert  eq, a2, a3
    rsr     a2, windowstart
    assert  eqi, a2, 1
    rfwu
2:
    rsr     a2, ps
    movi    a3, 0x4000f
    assert  eq, a2, a3
    rsr     a2, windowbase
    assert  eqi, a2, 0
    rsr     a2, windowstart
    assert  bsi.l, a2, 0
    assert  bsi.l, a2, (XCHAL_NUM_AREGS - (\window)) / 4
.endm

test underflow
    set_vector window_overflow_4, 0
    set_vector window_overflow_8, 0
    set_vector window_overflow_12, 0

    underflow_test 4
    underflow_test 8
    underflow_test 12
test_end


.macro retw_test window
    reset_window %(1 | (1 << ((XCHAL_NUM_AREGS - \window) / 4)))
    reset_ps

    ssai    2
    movi    a2, 1f
    slli    a2, a2, 2
    movi    a3, (\window) / 4
    src     a0, a3, a2
    retw
    test_fail
1:
    rsr     a2, ps
    movi    a3, 0x4000f
    assert  eq, a2, a3
    rsr     a2, windowbase
    movi    a3, (XCHAL_NUM_AREGS - (\window)) / 4
    assert  eq, a2, a3
    rsr     a2, windowstart
    assert  bci.l, a2, 0
    assert  bsi.l, a2, (XCHAL_NUM_AREGS - (\window)) / 4
.endm

test retw
    set_vector window_underflow_4, 0
    set_vector window_underflow_8, 0
    set_vector window_underflow_12, 0

    retw_test 4
    retw_test 8
    retw_test 12
test_end

test movsp
    set_vector kernel, 2f

    reset_window 1
    reset_ps
1:
    movsp   a2, a3
    test_fail
2:
    rsr     a2, exccause
    assert  eqi, a2, 5
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3

    set_vector kernel, 0

    reset_window %(0x1 | (1 << ((XCHAL_NUM_AREGS / 4) - 1)))
    reset_ps

    movsp   a2, a3
test_end

test rotw
    reset_window 0x4b
    reset_ps

    movi    a3, 0x10

    rotw    1
    rsr     a2, windowbase
    assert  eqi, a2, 1
    movi    a3, 0x11
    movi    a7, 0x12

    rotw    2
    rsr     a2, windowbase
    assert  eqi, a2, 3
    movi    a3, 0x13
    movi    a7, 0x14
    movi    a11, 0x15

    rotw    3
    rsr     a2, windowbase
    assert  eqi, a2, 6
    movi    a3, 0x16
    movi    a7, 0x17

#if XCHAL_NUM_AREGS == 32
    movi    a2, 0x44
    wsr     a2, windowstart
#elif XCHAL_NUM_AREGS == 64
    movi    a2, 0x4004
    wsr     a2, windowstart
    rotw    -8
#else
#error XCHAL_NUM_AREGS unsupported
#endif
    rsync

    movi    a2, 0x10
    assert  eq, a2, a11
    movi    a11, 0x18
    movi    a2, 0x11
    assert  eq, a2, a15
    movi    a15, 0x19

    rotw    4
    movi    a2, 0x12
    assert  eq, a2, a3
    movi    a2, 0x13
    assert  eq, a2, a7
    movi    a2, 0x14
    assert  eq, a2, a11
    movi    a2, 0x15
    assert  eq, a2, a15

    movi    a2, 0x5
    wsr     a2, windowstart
    rsync

    rotw    -2
    movi    a2, 0x18
    assert  eq, a2, a3
    movi    a2, 0x19
    assert  eq, a2, a7
test_end

.macro callw_test window
    call\window 2f
1:
    test_fail
    .align  4
2:
    rsr     a2, windowbase
    assert  eqi, a2, 0
    rsr     a2, ps
    movi    a3, 0x4000f | ((\window) << 14)
    assert  eq, a2, a3
    movi    a2, 1b
    slli    a2, a2, 2
    ssai    2
    movi    a3, (\window) / 4
    src     a2, a3, a2
    assert  eq, a2, a\window
.endm

test callw
    reset_window 0x1
    reset_ps

    callw_test 4
    callw_test 8
    callw_test 12
test_end


.macro entry_test window
    reset_window 0x1
    reset_ps
    movi    a2, 0x4000f | ((\window) << 14)
    wsr     a2, ps
    isync
    movi    a3, 0x12345678
    j       1f
    .align  4
1:
    entry   a3, 0x5678
    movi    a2, 0x12340000
    assert  eq, a2, a3
    rsr     a2, windowbase
    assert  eqi, a2, (\window) / 4
    rsr     a2, windowstart
    movi    a3, 1 | (1 << ((\window) / 4))
    assert  eq, a2, a3
    rotw    -(\window) / 4
.endm

test entry
    entry_test 4
    entry_test 8
    entry_test 12
test_end

.macro entry_overflow_test window, free, next_window
    set_vector window_overflow_4, 0
    set_vector window_overflow_8, 0
    set_vector window_overflow_12, 0
    set_vector window_overflow_\next_window, 10f

    movi    a2, \window
    movi    a2, \free
    movi    a2, \next_window
    reset_window %(1 | ((1 | (1 << ((\next_window) / 4))) << ((\free) / 4)))
    reset_ps
    movi    a2, 0x4000f | ((\window) << 14)
    wsr     a2, ps
    isync
    movi    a3, 0x12345678
    j       1f
    .align  4
1:
    entry   a3, 0x5678
    test_fail
    .align  4
10:
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    movi    a2, 2f
    wsr     a2, epc1

    rsr     a2, windowbase
    movi    a3, (\free) / 4
    assert  eq, a2, a3
    rfwo
2:
.endm

.macro all_entry_overflow_tests
    .irp window, 4, 8, 12
    .irp next_window, 4, 8, 12
    .irp free, 4, 8, 12
    .if \free <= \window
    entry_overflow_test \window, \free, \next_window
    .endif
    .endr
    .endr
    .endr
.endm

test entry_overflow
    all_entry_overflow_tests
test_end

#endif

test_suite_end
