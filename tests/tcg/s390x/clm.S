    .org 0x8e
program_interruption_code:
    .org 0x1d0                         /* program new PSW */
    .quad 0,pgm
    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lgrl %r0,op1
    clm %r0,6,op2
    jle failure
    lgrl %r1,bad_addr
    clm %r0,0,0(%r1)
failure:
    lpswe failure_psw
pgm:
    chhsi program_interruption_code,5  /* addressing exception? */
    jne failure
    lpswe success_psw
    .align 8
op1:
    .quad 0x1234567887654321
op2:
    .quad 0x3456789abcdef012
bad_addr:
    .quad 0xffffffff00000000
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
