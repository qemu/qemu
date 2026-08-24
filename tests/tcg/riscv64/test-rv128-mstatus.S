/*
 * Test rv128 mstatus update and access
 * Author: Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define srli(rd, rs1, imm)                .insn i  0b0010011, 0b101, rd, rs1, imm & 0b1111111

    .option norvc

    .text
    .globl _start
_start:
    csrr    s0, misa
    srli    (s1, s0, 126)
    li      s0, 3
    bne     s1, s0, fail

    # Check UXL and SXL are set to RV128 too
    csrr    s0, mstatus
    srli    (s1, s0, 32)
    andi    s1, s1, 0xf
    li      s0, 0xf
    bne     s1, s0, fail

    # Mark FPU dirty to check for SD
    .equiv  MSTATUS_FS, 0x00006000
    li      s0, MSTATUS_FS
    csrs    mstatus, s0
    # Do some useless computation
    fmadd.s f3, f2, f1, f0
    fmax.s  f0, f1, f2
    # Check SD has changed where expected
    csrr    s0, mstatus
    srli    (s1, s0, 127)
    li      s0, 1
    bne     s1, s0, fail
    # Mark FPU clean (2 in FS place)
    li      s0, ~0x00004000
    csrc    mstatus, s0
    csrr    s0, mstatus
    srli    (s1, s0, 13)
    andi    s1, s1, 3
    li      s0, 2
    bne     s1, s0, fail
    li      a0, 0
    j       _exit
fail:
    li      a0, 1
_exit:
    lla     a1, semiargs
    li      t0, 0x20026 # ADP_Stopped_ApplicationExit
    sd      t0, 0(a1)
    sd      a0, 8(a1)
    li      a0, 0x20    # TARGET_SYS_EXIT_EXTENDED

    # Semihosting call sequence
    .balign 16
    slli   zero, zero, 0x1f
    ebreak
    srai   zero, zero, 0x7
    j      .

    .data
    .balign 16
semiargs:
    .space  16
