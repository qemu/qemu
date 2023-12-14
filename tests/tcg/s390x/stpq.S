    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lgrl %r0,value
    lgrl %r1,value+8
    stpq %r0,stored_value
    clc stored_value(16),value
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw
    .align 16
value:
    .quad 0x1234567887654321, 0x8765432112345678
stored_value:
    .quad 0, 0
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
