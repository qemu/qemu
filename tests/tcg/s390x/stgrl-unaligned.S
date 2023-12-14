/*
 * Test STGRL to a non-doubleword aligned address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .globl test
test:
    stgrl %r1,unaligned

    .align 8
    .globl expected_old_psw
expected_old_psw:
    .quad 0x180000000,test
    .long 0
unaligned:
    .quad 0
