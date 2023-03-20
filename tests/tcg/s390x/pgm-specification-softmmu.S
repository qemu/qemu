/*
 * Common softmmu code for specification exception testing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .section .head
    .org 0x8d
ilc:
    .org 0x8e
program_interruption_code:
    .org 0x150
program_old_psw:
    .org 0x1D0                         /* program new PSW */
    .quad 0x180000000,pgm              /* 64-bit mode */
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpswe test_psw

pgm:
    chhsi program_interruption_code,0x6          /* PGM_SPECIFICATION? */
    jne failure
    lg %r0,expected_old_psw+8                    /* ilc adjustment */
    llgc %r1,ilc
    agr %r0,%r1
    stg %r0,expected_old_psw+8
    clc expected_old_psw(16),program_old_psw     /* correct location? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw

    .align 8
test_psw:
    .quad 0x180000000,test             /* 64-bit mode */
success_psw:
    .quad 0x2000180000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000180000000,0            /* disabled wait */
