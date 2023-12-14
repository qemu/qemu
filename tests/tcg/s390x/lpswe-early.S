/*
 * Test early exception recognition using LPSWE.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .org 0x8d
ilc:
    .org 0x8e
program_interruption_code:
    .org 0x150
program_old_psw:
    .org 0x1D0                         /* program new PSW */
    .quad 0,pgm
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpswe bad_psw
    j failure

pgm:
    chhsi program_interruption_code,0x6          /* specification exception? */
    jne failure
    cli ilc,0                                    /* ilc zero? */
    jne failure
    clc program_old_psw(16),bad_psw              /* correct old PSW? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw

    .align 8
bad_psw:
    .quad 0x8000000000000000,0xfedcba9876543210  /* bit 0 set */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
