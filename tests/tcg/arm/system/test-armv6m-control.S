/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test that Cortex-M0 does not implement CONTROL.nPRIV. */

.syntax unified
.cpu cortex-m0
.thumb

#define SRAM_BASE 0x20000000
#define SRAM_SIZE (16 * 1024)

#define semihosting_call bkpt 0xab
#define SYS_EXIT 0x18

vector_table:
    .word SRAM_BASE + SRAM_SIZE
    .word reset + 1
    .word 0
    .word failure + 1
    .rept 7
    .word 0
    .endr
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .rept 32
    .word 0
    .endr

.equ exc_reset_thumb, reset + 1
.global exc_reset_thumb
reset:
    /* Thread mode starts privileged. */
    mrs r0, control
    movs r1, 1
    tst r0, r1
    bne failure

    /* Cortex-M0 does not implement the optional privilege extension. */
    movs r0, 1
    msr control, r0
    isb
    mrs r0, control
    tst r0, r1
    bne failure

success:
    movs r0, 1
    b exit

failure:
    movs r0, 0

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
