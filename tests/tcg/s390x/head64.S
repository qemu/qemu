/*
 * Startup code for multiarch tests.
 * Reuses the pc-bios/s390-ccw implementation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define main main_pre
#include "../../../pc-bios/s390-ccw/start.S"
#undef main

main_pre:
    aghi %r15,-160                     /* reserve stack for C code */
    brasl %r14,sclp_setup
    brasl %r14,main
    larl %r1,success_psw               /* check main() return code */
    ltgr %r2,%r2
    je 0f
    larl %r1,failure_psw
0:
    lpswe 0(%r1)

    .align 8
success_psw:
    .quad 0x2000180000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000180000000,0            /* disabled wait */

    .section .bss
    .align 0x1000
stack:
    .skip 0x8000
