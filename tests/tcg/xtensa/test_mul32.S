#include "macros.inc"

test_suite mul32

#if XCHAL_HAVE_MUL32

test mull
    movi    a2, 0x137f5a5a
    mov     a3, a2
    movi    a4, 0xa5a5137f
    movi    a6, 0x5de480a6
    mull    a5, a2, a4
    assert  eq, a5, a6
    mull    a2, a2, a4
    assert  eq, a2, a6
    mull    a3, a4, a3
    assert  eq, a3, a6
test_end

#endif

/* unfortunately dc232b doesn't have muluh/mulsh*/

test_suite_end
