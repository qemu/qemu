/*
 * Test ARMv6-M UNDEFINED 32-bit instructions
 *
 * Copyright 2018 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 */

/*
 * Test that UNDEFINED 32-bit instructions fault as expected.  This is an
 * interesting test because ARMv6-M shares code with its more fully-featured
 * siblings and it's necessary to verify that its limited instruction set is
 * emulated correctly.
 *
 * The emulator must be invoked with -semihosting so that the test case can
 * terminate with exit code 0 on success or 1 on failure.
 *
 * Failures can be debugged with -d in_asm,int,exec,cpu and the
 * gdbstub (-S -s).
 */

.syntax unified
.cpu cortex-m0
.thumb

/*
 * Memory map
 */
#define SRAM_BASE 0x20000000
#define SRAM_SIZE (16 * 1024)

/*
 * Semihosting interface on ARM T32
 * See "Semihosting for AArch32 and AArch64 Version 2.0 Documentation" by ARM
 */
#define semihosting_call bkpt 0xab
#define SYS_EXIT 0x18

vector_table:
    .word SRAM_BASE + SRAM_SIZE /* 0. SP_main */
    .word exc_reset_thumb       /* 1. Reset */
    .word 0                     /* 2. NMI */
    .word exc_hard_fault_thumb  /* 3. HardFault */
    .rept 7
    .word 0                     /* 4-10. Reserved */
    .endr
    .word 0                     /* 11. SVCall */
    .word 0                     /* 12. Reserved */
    .word 0                     /* 13. Reserved */
    .word 0                     /* 14. PendSV */
    .word 0                     /* 15. SysTick */
    .rept 32
    .word 0                     /* 16-47. External Interrupts */
    .endr

exc_reset:
.equ exc_reset_thumb, exc_reset + 1
.global exc_reset_thumb
    /* The following 32-bit UNDEFINED instructions are tested by executing
     * them.  The HardFault exception handler should execute and return to
     * the next test case.  If no exception is raised the test fails.
     */

    /* Table A5-9 32-bit Thumb encoding */
    .short 0b1110100000000000
    .short 0b0000000000000000
    b not_reached
    .short 0b1110100000000000
    .short 0b1000000000000000
    b not_reached
    .short 0b1111100000000000
    .short 0b0000000000000000
    b not_reached
    .short 0b1111100000000000
    .short 0b1000000000000000
    b not_reached
    .short 0b1111000000000000
    .short 0b0000000000000000
    b not_reached

    /* Table A5-10 Branch and miscellaneous control instructions */
    .short 0b1111011111110000
    .short 0b1010000000000000
    b not_reached

    /* The following are valid 32-bit instructions that must not raise a
     * HardFault.
     */

    /* B4.2.3 Move to Special Register (moves to IPSR are ignored) */
    msr ipsr, r0
    b 1f
    b not_reached
1:
    /* B4.2.2 Move from Special Register */
    mrs r0, ipsr
    b 1f
    b not_reached
1:
    /* A6.7.13 Branch with Link (immediate) */
    bl 1f
1:
    b 1f
    b not_reached
1:
    /* A6.7.21 Data Memory Barrier */
    dmb
    b 1f
    b not_reached
1:
    /* A6.7.22 Data Synchronization Barrier */
    dsb
    b 1f
    b not_reached
1:
    /* A6.7.24 Instruction Memory Barrier */
    isb
    b 1f
    b not_reached
1:

    /* Success! */
    movs r0, 1
    b exit

not_reached: /* Failure :( */
    movs r0, 0
    b exit

/* When a HardFault occurs, return to pc+6 (test cases are 3 halfwords long) */
exc_hard_fault:
.equ exc_hard_fault_thumb, exc_hard_fault + 1
.global exc_hard_fault_thumb
    ldr r0, [sp, 0x18]
    adds r0, 6
    str r0, [sp, 0x18]
    bx lr

/*
 * exit: Terminate emulator
 * @r0: 0 - failure, 1 - success
 */
exit:
    movs r1, 0
    cmp r0, 1
    bne 1f
    ldr r1, ADP_Stopped_ApplicationExit
1:
    movs r0, SYS_EXIT
    semihosting_call
.align 2
ADP_Stopped_ApplicationExit:
    .word 0x20026
