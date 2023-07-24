    .org 0x8e
program_interruption_code:
    .org 0x1d0                         /* program new PSW */
    .quad 0,pgm
    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lmg %r0,%r1,cksm_args
    cksm %r2,%r0
    c %r2,cksm_exp
    jne failure
    .insn rre,0xb2410000,%r2,%r15      /* cksm %r2,%r15 */
failure:
    lpswe failure_psw
pgm:
    chhsi program_interruption_code,6  /* specification exception? */
    jne failure
    lpswe success_psw
cksm_args:
    .quad cksm_buf, 16
cksm_buf:
    .quad 0xaaaabbbbcccc0000, 0x12345678
cksm_exp:
    .long 0x89ab1234
    .align 8
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
