#include "core-isa.h"

.macro vector name

.section .vector.\name, "ax"
.global vector_\name
vector_\name\():
    j       1f
    .literal_position
1:
    wsr     a0, excsave1
    movi    a0, 1f
    ret.n

.section .vector.\name\().text, "ax"
    .literal_position
1:
    rsr     a0, excsave1
    wsr     a2, excsave1
    movi    a2, handler_\name
    l32i    a2, a2, 0
    beqz    a2, 1f
    jx      a2
1:
    movi    a3, 1b
    movi    a2, 1
    simcall

.align 4
.global handler_\name
handler_\name\(): .word 0

.endm

#if XCHAL_HAVE_WINDOWED
vector window_overflow_4
vector window_overflow_8
vector window_overflow_12
vector window_underflow_4
vector window_underflow_8
vector window_underflow_12
#endif

#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 2
vector level2
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 3
vector level3
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 4
vector level4
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 5
vector level5
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 6
vector level6
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 7
vector level7
#endif

vector kernel
vector user
vector double
