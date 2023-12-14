/*
 * Test STRL to a non-word aligned address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .globl test
test:
    strl %r1,unaligned

    .align 8
    .globl expected_old_psw
expected_old_psw:
    .quad 0x180000000,test
    .short 0
unaligned:
    .long 0
