/*
 * Test clock comparator.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .org 0x130
ext_old_psw:
    .org 0x1b0
ext_new_psw:
    .quad 0x180000000, _ext            /* 64-bit mode */
    .org 0x1d0
pgm_new_psw:
    .quad 0x2000000000000,0            /* disabled wait */
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpswe start31_psw
_start31:
    stctg %c0,%c0,c0
    oi c0+6,8                          /* set clock-comparator subclass mask */
    lctlg %c0,%c0,c0

0:
    brasl %r14,_f                      /* %r14's most significant bit is 1 */
    jg 0b
_f:
    br %r14                            /* it must not end up in ext_old_psw */

_ext:
    stg %r0,ext_saved_r0

    lg %r0,ext_counter
    aghi %r0,1
    stg %r0,ext_counter

    cgfi %r0,0x1000
    jnz 0f
    lpswe success_psw
0:

    stck clock
    lg %r0,clock
    agfi %r0,0x40000                   /* 64us * 0x1000 =~ 0.25s */
    stg %r0,clock
    sckc clock

    lg %r0,ext_saved_r0
    lpswe ext_old_psw

    .align 8
start31_psw:
    .quad 0x100000080000000,_start31   /* EX, 31-bit mode */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
c0:
    .skip 8
clock:
    .quad 0
ext_counter:
    .quad 0
ext_saved_r0:
    .skip 8
