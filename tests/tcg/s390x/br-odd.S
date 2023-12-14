/*
 * Test BRanching to a non-mapped odd address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .globl test
test:
    lgrl %r1,odd_addr
    br %r1

    .align 8
odd_addr:
    .quad 0xDDDDDDDDDDDDDDDD
    .globl expected_old_psw
expected_old_psw:
    .quad 0x180000000,0xDDDDDDDDDDDDDDDD
