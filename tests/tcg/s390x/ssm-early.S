/*
 * Test early exception recognition using SSM.
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
    ssm ssm_op
expected_pswa:
    j failure

pgm:
    chhsi program_interruption_code,0x6          /* specification exception? */
    jne failure
    cli ilc,4                                    /* ilc for SSM? */
    jne failure
    clc program_old_psw(16),expected_old_psw     /* correct old PSW? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw

ssm_op:
    .byte 0x20                                   /* bit 2 set */
    .align 8
expected_old_psw:
    .quad 0x2000000180000000,expected_pswa       /* bit 2 set */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
