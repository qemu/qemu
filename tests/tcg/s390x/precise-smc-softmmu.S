/*
 * Test s390x-softmmu precise self-modifying code handling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .org 0x8e
program_interruption_code:
    .org 0x150
program_old_psw:
    .org 0x1D0                         /* program new PSW */
    .quad 0x180000000,pgm              /* 64-bit mode */
    .org 0x200                         /* lowcore padding */
    .globl _start
_start:
    lctlg %c0,%c0,c0
    lghi %r0,15

    /* Test 1: replace sgr with agr. */
    lghi %r1,21
    vl %v0,patch1
    jg 1f                              /* start a new TB */
0:
    .org . + 6                         /* pad patched code to 16 bytes */
1:
    vstl %v0,%r0,0b                    /* start writing before TB */
    sgr %r1,%r1                        /* this becomes `agr %r1,%r1` */
    cgijne %r1,42,failure

    /* Test 2: replace agr with division by zero. */
    vl %v0,patch2
    jg 1f                              /* start a new TB */
0:
    .org . + 6                         /* pad patched code to 16 bytes */
1:
    vstl %v0,%r0,0b                    /* start writing before TB */
    sgr %r1,%r1                        /* this becomes `d %r0,zero` */
failure:
    lpswe failure_psw

pgm:
    chhsi program_interruption_code,0x9          /* divide exception? */
    jne failure
    clc program_old_psw(16),expected_old_psw2    /* correct old PSW? */
    jne failure
    lpswe success_psw

patch1:
    .fill 12                           /* replaces padding and stpq */
    agr %r1,%r1                        /* replaces sgr */
patch2:
    .fill 12                           /* replaces padding and stpq */
    d %r0,zero                         /* replaces sgr */
zero:
    .long 0
expected_old_psw2:
    .quad 0x200180000000,failure       /* cc is from addition */
    .align 8
c0:
    .quad 0x60000                      /* AFP, VX */
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
