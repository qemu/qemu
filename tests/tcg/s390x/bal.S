    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lpswe start24_psw
_start24:
    lgrl %r0,initial_r0
    lgrl %r1,expected_r0
    bal %r0,0f
0:
    cgrjne %r0,%r1,1f
    lpswe success_psw
1:
    lpswe failure_psw
    .align 8
start24_psw:
    .quad 0x160000000000,_start24      /* 24-bit mode, cc = 1, pm = 6 */
initial_r0:
    .quad 0x1234567887654321
expected_r0:
    .quad 0x1234567896000000 + 0b      /* ilc = 2, cc = 1, pm = 6 */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
