.macro vector name

.section .vector.\name
    j       1f
.section .vector.\name\().text
1:
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

vector window_overflow_4
vector window_overflow_8
vector window_overflow_12
vector window_underflow_4
vector window_underflow_8
vector window_underflow_12

vector level2
vector level3
vector level4
vector level5
vector level6
vector level7

vector kernel
vector user
vector double
