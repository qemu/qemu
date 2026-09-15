/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test Armv6-M NVIC register behavior. */

.syntax unified
.cpu cortex-m0
.thumb

#define SRAM_BASE 0x20000000
#define SRAM_SIZE (16 * 1024)

#define NVIC_ICTR  0xe000e004
#define NVIC_IABR0 0xe000e300
#define NVIC_IPR0  0xe000e400

#define semihosting_call bkpt 0xab
#define SYS_EXIT 0x18

vector_table:
    .word SRAM_BASE + SRAM_SIZE
    .word reset + 1
    .word 0
    .word failure + 1
    .rept 44
    .word 0
    .endr

.equ exc_reset_thumb, reset + 1
.global exc_reset_thumb
reset:
    /* ICTR is reserved and reads as zero on Armv6-M. */
    ldr r0, =NVIC_ICTR
    ldr r1, [r0]
    cmp r1, 0
    bne failure

    /* The Active Bit Register is reserved and behaves as RAZ/WI. */
    ldr r0, =NVIC_IABR0
    movs r1, 1
    str r1, [r0]
    ldr r1, [r0]
    cmp r1, 0
    bne failure

    /* Cortex-M0 implements the two most significant priority bits. */
    ldr r0, =NVIC_IPR0
    movs r1, 0xff
    strb r1, [r0]
    ldrb r1, [r0]
    cmp r1, 0xc0
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
