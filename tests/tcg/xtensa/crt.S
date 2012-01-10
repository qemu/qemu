.section .init
    j       1f
.section .init.text
1:
    movi    a2, _start
    jx      a2

.text
.global _start
_start:
    movi    a2, 1
    wsr     a2, windowstart
    movi    a2, 0
    wsr     a2, windowbase
    movi    a1, _fstack
    movi    a2, 0x4000f
    wsr     a2, ps
    isync

    call0   main

    mov     a3, a2
    movi    a2, 1
    simcall
