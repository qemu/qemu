/*
 * Boot entry point and assembler functions for aarch64 tests.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#define __ASSEMBLY__
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>

.section .init

.globl start
start:
	/*
	 * bootloader params are in x0-x3
	 * The physical address of the dtb is in x0, x1-x3 are reserved
	 * See the kernel doc Documentation/arm64/booting.txt
	 */
	mov	x4, #1
	msr	spsel, x4
	isb
	adr     x4, stackptr
	mov	sp, x4
	stp	x0, x1, [sp, #-16]!

	/* Enable FP/ASIMD */
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* set up exception handling */
	bl	exceptions_init

	/* complete setup */
	ldp	x0, x1, [sp], #16
	bl	setup

	/* run the test */
	adr	x0, __argc
	ldr	x0, [x0]
	adr	x1, __argv
	bl	main
	bl	exit
	b	halt

exceptions_init:
	adr	x0, vector_table
	msr	vbar_el1, x0
	isb
	ret

.text

.globl secondary_entry
secondary_entry:
	/* Enable FP/ASIMD */
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* set up exception handling */
	bl	exceptions_init

	/* enable the MMU */
	adr	x0, mmu_idmap
	ldr	x0, [x0]
	bl	asm_mmu_enable

	/* set the stack */
	adr	x1, secondary_data
	ldr	x0, [x1]
	mov	sp, x0

	/* finish init in C code */
	bl	secondary_cinit

	/* x0 is now the entry function, run it */
	br	x0

.globl halt
halt:
1:	wfi
	b	1b

/*
 * asm_mmu_enable
 *   Inputs:
 *     x0 is the base address of the translation table
 *   Outputs: none
 *
 * Adapted from
 *   arch/arm64/kernel/head.S
 *   arch/arm64/mm/proc.S
 */

/*
 * Memory region attributes for LPAE:
 *
 *   n = AttrIndx[2:0]
 *                      n       MAIR
 *   DEVICE_nGnRnE      000     00000000
 *   DEVICE_nGnRE       001     00000100
 *   DEVICE_GRE         010     00001100
 *   NORMAL_NC          011     01000100
 *   NORMAL             100     11111111
 */
#define MAIR(attr, mt) ((attr) << ((mt) * 8))

.globl asm_mmu_enable
asm_mmu_enable:
	ic	iallu			// I+BTB cache invalidate
	tlbi	vmalle1is		// invalidate I + D TLBs
	dsb	ish

	/* TCR */
	ldr	x1, =TCR_TxSZ(VA_BITS) |		\
		     TCR_TG0_64K | TCR_TG1_64K |	\
		     TCR_IRGN_WBWA | TCR_ORGN_WBWA |	\
		     TCR_SHARED
	mrs	x2, id_aa64mmfr0_el1
	bfi	x1, x2, #32, #3
	msr	tcr_el1, x1

	/* MAIR */
	ldr	x1, =MAIR(0x00, MT_DEVICE_nGnRnE) |	\
		     MAIR(0x04, MT_DEVICE_nGnRE) |	\
		     MAIR(0x0c, MT_DEVICE_GRE) |	\
		     MAIR(0x44, MT_NORMAL_NC) |		\
		     MAIR(0xff, MT_NORMAL)
	msr	mair_el1, x1

	/* TTBR0 */
	msr	ttbr0_el1, x0
	isb

	/* SCTLR */
	mrs	x1, sctlr_el1
	orr	x1, x1, SCTLR_EL1_C
	orr	x1, x1, SCTLR_EL1_I
	orr	x1, x1, SCTLR_EL1_M
	msr	sctlr_el1, x1
	isb

	ret

.globl asm_mmu_disable
asm_mmu_disable:
	mrs	x0, sctlr_el1
	bic	x0, x0, SCTLR_EL1_M
	msr	sctlr_el1, x0
	isb
	ret

/*
 * Vectors
 * Adapted from arch/arm64/kernel/entry.S
 */
.macro vector_stub, name, vec
\name:
	stp	 x0,  x1, [sp, #-S_FRAME_SIZE]!
	stp	 x2,  x3, [sp,  #16]
	stp	 x4,  x5, [sp,  #32]
	stp	 x6,  x7, [sp,  #48]
	stp	 x8,  x9, [sp,  #64]
	stp	x10, x11, [sp,  #80]
	stp	x12, x13, [sp,  #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]

	str	x30, [sp, #S_LR]

	.if \vec >= 8
	mrs	x1, sp_el0
	.else
	add	x1, sp, #S_FRAME_SIZE
	.endif
	str	x1, [sp, #S_SP]

	mrs	x1, elr_el1
	mrs	x2, spsr_el1
	stp	x1, x2, [sp, #S_PC]

	mov	x0, \vec
	mov	x1, sp
	mrs	x2, esr_el1
	bl	do_handle_exception

	ldp	x1, x2, [sp, #S_PC]
	msr	spsr_el1, x2
	msr	elr_el1, x1

	.if \vec >= 8
	ldr	x1, [sp, #S_SP]
	msr	sp_el0, x1
	.endif

	ldr	x30, [sp, #S_LR]

	ldp	x28, x29, [sp, #224]
	ldp	x26, x27, [sp, #208]
	ldp	x24, x25, [sp, #192]
	ldp	x22, x23, [sp, #176]
	ldp	x20, x21, [sp, #160]
	ldp	x18, x19, [sp, #144]
	ldp	x16, x17, [sp, #128]
	ldp	x14, x15, [sp, #112]
	ldp	x12, x13, [sp,  #96]
	ldp	x10, x11, [sp,  #80]
	ldp	 x8,  x9, [sp,  #64]
	ldp	 x6,  x7, [sp,  #48]
	ldp	 x4,  x5, [sp,  #32]
	ldp	 x2,  x3, [sp,  #16]
	ldp	 x0,  x1, [sp], #S_FRAME_SIZE

	eret
.endm

vector_stub	el1t_sync,     0
vector_stub	el1t_irq,      1
vector_stub	el1t_fiq,      2
vector_stub	el1t_error,    3

vector_stub	el1h_sync,     4
vector_stub	el1h_irq,      5
vector_stub	el1h_fiq,      6
vector_stub	el1h_error,    7

vector_stub	el0_sync_64,   8
vector_stub	el0_irq_64,    9
vector_stub	el0_fiq_64,   10
vector_stub	el0_error_64, 11

vector_stub	el0_sync_32,  12
vector_stub	el0_irq_32,   13
vector_stub	el0_fiq_32,   14
vector_stub	el0_error_32, 15

.section .text.ex

.macro ventry, label
.align 7
	b	\label
.endm

.align 11
vector_table:
	ventry	el1t_sync			// Synchronous EL1t
	ventry	el1t_irq			// IRQ EL1t
	ventry	el1t_fiq			// FIQ EL1t
	ventry	el1t_error			// Error EL1t

	ventry	el1h_sync			// Synchronous EL1h
	ventry	el1h_irq			// IRQ EL1h
	ventry	el1h_fiq			// FIQ EL1h
	ventry	el1h_error			// Error EL1h

	ventry	el0_sync_64			// Synchronous 64-bit EL0
	ventry	el0_irq_64			// IRQ 64-bit EL0
	ventry	el0_fiq_64			// FIQ 64-bit EL0
	ventry	el0_error_64			// Error 64-bit EL0

	ventry	el0_sync_32			// Synchronous 32-bit EL0
	ventry	el0_irq_32			// IRQ 32-bit EL0
	ventry	el0_fiq_32			// FIQ 32-bit EL0
	ventry	el0_error_32			// Error 32-bit EL0
