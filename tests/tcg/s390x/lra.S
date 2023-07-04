    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lgrl %r1,initial_r1
    lra %r1,0(%r1)
    cgrl %r1,expected_r1
    jne 1f
    lpswe success_psw
1:
    lpswe failure_psw
    .align 8
initial_r1:
    .quad 0x8765432112345678
expected_r1:
    .quad 0x8765432180000038           /* ASCE type exception */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
