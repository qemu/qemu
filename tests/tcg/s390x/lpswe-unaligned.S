/*
 * Test LPSWE from a non-doubleword aligned address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .globl test
test:
    larl %r1,unaligned
fail:
    lpswe 0(%r1)

    .align 8
    .globl expected_old_psw
expected_old_psw:
    .quad 0x180000000,fail
    .long 0
unaligned:
    .quad 0
