/*
 * Test rv128 sstatus update and access
 * Author: Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define srli(rd, rs1, imm)                .insn i  0b0010011, 0b101, rd, rs1, imm & 0b1111111

    .option norvc

    .text
    .globl _start
_start:
    // Check RV128 is running
    csrr     s0, misa
    srli     (s1, s0, 126)
    li       s0, 3
    bne      s1, s0, fail

    // Minimal pmp settings
#define PMP_NAPOT 0x18
#define PMP_R     0x01
#define PMP_W     0x02
#define PMP_X     0x04

    li       s0, -1
    csrw     pmpaddr0, s0
    li       s0, PMP_NAPOT | PMP_R | PMP_W | PMP_X
    csrw     pmpcfg0, s0

    la       s0, backm
    csrw     mtvec, s0

    // Set MPP to supervisor
    csrr     s0, mstatus
    li       s1, ~0x1800 // mask
    and      s1, s1, s0
    li       s0, 0x0800  // supervisor
    or       s1, s1, s0
    csrw     mstatus, s1
    // Change to supervisor mode
    la       s0, 1f
    csrw     mepc, s0
    mret
1:
    // Check UXL is set to RV128
    csrr     s0, sstatus
    srli     (s1, s0, 32)
    andi     s1, s1, 0x3
    li       s0, 3
    bne      s1, s0, fail
    // Check SPP if is set to 1, since we
    // come from machine mode
    srli     (s1, s0, 8)
    andi     s1, s1, 0x1
    // Actually this check fails, so test 0
    li       s0, 0
    bne      s1, s0, fail

    // Mark FPU dirty to check for SD
    .equiv   MSTATUS_FS, 0x00006000
    li       s0,  MSTATUS_FS
    csrs     sstatus, s0
    // Do some fun useless computation
    fmadd.s  f3, f2, f1, f0
    fmax.s   f0, f1, f2
    // Check SD has changed where expected
    csrr     s0, sstatus
    srli     (s1, s0, 127)
    li       s0, 1
    bne      s1, s0, fail
    // Mark FPU clean (2 in FS place)
    li       s0, ~0x00004000
    csrc     sstatus, s0
    csrr     s0, sstatus
    srli     (s1, s0, 127)
    li       s0, 0
    bne      s1, s0, fail

    // Let's go back to machine mode to exit
    ecall

    // mtvec bit 1:0 indicate mode, 00 is Direct, what
    // we want in this simple test
    .align 4
backm:
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
