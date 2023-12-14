/*
 * Test the LPSW instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .org 0x140
svc_old_psw:
    .org 0x1c0                         /* supervisor call new PSW */
    .quad 0x80000000,svc               /* 31-bit mode */
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpsw short_psw
lpsw_target:
    svc 0
expected_pswa:
    j failure

svc:
    clc svc_old_psw(16),expected_psw   /* correct full PSW? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw

    .align 8
short_psw:
    .long 0x90001,0x80000000+lpsw_target         /* problem state,
                                                    64-bit mode */
expected_psw:
    .quad 0x1000180000000,expected_pswa          /* corresponds to short_psw */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
