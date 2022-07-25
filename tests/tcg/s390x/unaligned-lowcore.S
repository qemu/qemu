    .org 0x1D0                         /* program new PSW */
    .quad 0x2000000000000,0            /* disabled wait */
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lctlg %c0,%c0,_c0
    vst %v0,_unaligned
    lpswe quiesce_psw

    .align 8
quiesce_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
_c0:
    .quad 0x10060000                   /* lowcore protection, AFP, VX */

    .byte 0
_unaligned:
    .octa 0
