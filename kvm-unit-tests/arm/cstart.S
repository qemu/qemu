/*
 * Boot entry point and assembler functions for armv7 tests.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#define __ASSEMBLY__
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/cp15.h>

#define THREAD_START_SP ((THREAD_SIZE - S_FRAME_SIZE * 8) & ~7)

.arm

.section .init

.globl start
start:
	/*
	 * set stack, making room at top of stack for cpu0's
	 * exception stacks. Must start wtih stackptr, not
	 * stacktop, so the thread size masking (shifts) work.
	 */
	ldr	sp, =stackptr
	lsr	sp, #THREAD_SHIFT
	lsl	sp, #THREAD_SHIFT
	add	sp, #THREAD_START_SP

	/*
	 * save sp before pushing anything on the stack
	 * lr makes a good temp register right now
	 */
	mov	lr, sp

	/*
	 * bootloader params are in r0-r2
	 * See the kernel doc Documentation/arm/Booting
	 *   r0 = 0
	 *   r1 = machine type number
	 *   r2 = physical address of the dtb
	 *
	 * As we have no need for r0's nor r1's value, then
	 * put the dtb in r0. This allows setup to be consistent
	 * with arm64.
	 */
	mov	r0, r2
	push	{r0-r1}

	/* set up vector table and mode stacks */
	mov	r0, lr			@ lr is stack top (see above),
					@ which is the exception stacks base
	bl	exceptions_init

	/* complete setup */
	pop	{r0-r1}
	bl	setup

	/* run the test */
	ldr	r0, =__argc
	ldr	r0, [r0]
	ldr	r1, =__argv
	bl	main
	bl	exit
	b	halt


.macro set_mode_stack mode, stack
	add	\stack, #S_FRAME_SIZE
	msr	cpsr_c, #(\mode | PSR_I_BIT | PSR_F_BIT)
	isb
	mov	sp, \stack
.endm

exceptions_init:
	mrc	p15, 0, r2, c1, c0, 0	@ read SCTLR
	bic	r2, #CR_V		@ SCTLR.V := 0
	mcr	p15, 0, r2, c1, c0, 0	@ write SCTLR
	ldr	r2, =vector_table
	mcr	p15, 0, r2, c12, c0, 0	@ write VBAR

	mrs	r2, cpsr

	/* first frame reserved for svc mode */
	set_mode_stack	UND_MODE, r0
	set_mode_stack	ABT_MODE, r0
	set_mode_stack	IRQ_MODE, r0
	set_mode_stack	FIQ_MODE, r0

	msr	cpsr_cxsf, r2		@ back to svc mode
	isb
	mov	pc, lr

.text

.global secondary_entry
secondary_entry:
	/* enable the MMU */
	mov	r1, #0
	ldr	r0, =mmu_idmap
	ldr	r0, [r0]
	bl	asm_mmu_enable

	/*
	 * Set the stack, and set up vector table
	 * and exception stacks. Exception stacks
	 * space starts at stack top and grows up.
	 */
	ldr	r1, =secondary_data
	ldr	r0, [r1]
	mov	sp, r0
	bl	exceptions_init

	/* finish init in C code */
	bl	secondary_cinit

	/* r0 is now the entry function, run it */
	mov	pc, r0

.globl halt
halt:
1:	wfi
	b	1b

/*
 * asm_mmu_enable
 *   Inputs:
 *     (r0 - lo, r1 - hi) is the base address of the translation table
 *   Outputs: none
 */
.equ	PRRR,	0xeeaa4400		@ MAIR0 (from Linux kernel)
.equ	NMRR,	0xff000004		@ MAIR1 (from Linux kernel)
.globl asm_mmu_enable
asm_mmu_enable:
	/* TTBCR */
	mrc	p15, 0, r2, c2, c0, 2
	orr	r2, #(1 << 31)		@ TTB_EAE
	mcr	p15, 0, r2, c2, c0, 2

	/* MAIR */
	ldr	r2, =PRRR
	mcr	p15, 0, r2, c10, c2, 0
	ldr	r2, =NMRR
	mcr	p15, 0, r2, c10, c2, 1

	/* TTBR0 */
	mcrr	p15, 0, r0, r1, c2
	isb

	/* SCTLR */
	mrc	p15, 0, r2, c1, c0, 0
	orr	r2, #CR_C
	orr	r2, #CR_I
	orr	r2, #CR_M
	mcr	p15, 0, r2, c1, c0, 0
	isb

	mov     pc, lr

.globl asm_mmu_disable
asm_mmu_disable:
	/* SCTLR */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, #CR_M
	mcr	p15, 0, r0, c1, c0, 0
	isb
	mov     pc, lr

/*
 * Vector stubs
 * Simplified version of the Linux kernel implementation
 *   arch/arm/kernel/entry-armv.S
 *
 * Each mode has an S_FRAME_SIZE sized memory region,
 * and the mode's stack pointer has been initialized
 * to the base of that region in exceptions_init.
 */
.macro vector_stub, name, vec, mode, correction=0
.align 5
vector_\name:
.if \correction
	sub	lr, lr, #\correction
.endif
	/*
	 * Save r0, r1, lr_<exception> (parent PC)
	 * and spsr_<exception> (parent CPSR)
	 */
	str	r0, [sp, #S_R0]
	str	r1, [sp, #S_R1]
	str	lr, [sp, #S_PC]
	mrs	r0, spsr
	str	r0, [sp, #S_PSR]

	/* Prepare for SVC32 mode. */
	mrs	r0, cpsr
	bic	r0, #MODE_MASK
	orr	r0, #SVC_MODE
	msr	spsr_cxsf, r0

	/* Branch to handler in SVC mode */
	mov	r0, #\vec
	mov	r1, sp
	ldr	lr, =vector_common
	movs	pc, lr
.endm

vector_stub 	rst,	0, UND_MODE
vector_stub	und,	1, UND_MODE
vector_stub	pabt,	3, ABT_MODE, 4
vector_stub	dabt,	4, ABT_MODE, 8
vector_stub	irq,	6, IRQ_MODE, 4
vector_stub	fiq,	7, FIQ_MODE, 4

.align 5
vector_svc:
	/*
	 * Save r0, r1, lr_<exception> (parent PC)
	 * and spsr_<exception> (parent CPSR)
	 */
	push	{ r1 }
	lsr	r1, sp, #THREAD_SHIFT
	lsl	r1, #THREAD_SHIFT
	add	r1, #THREAD_START_SP
	str	r0, [r1, #S_R0]
	pop	{ r0 }
	str	r0, [r1, #S_R1]
	str	lr, [r1, #S_PC]
	mrs	r0, spsr
	str	r0, [r1, #S_PSR]

	/*
	 * Branch to handler, still in SVC mode.
	 * r0 := 2 is the svc vector number.
	 */
	mov	r0, #2
	ldr	lr, =vector_common
	mov	pc, lr

vector_common:
	/* make room for pt_regs */
	sub	sp, #S_FRAME_SIZE
	tst	sp, #4			@ check stack alignment
	subne	sp, #4

	/* store registers r0-r12 */
	stmia	sp, { r0-r12 }		@ stored wrong r0 and r1, fix later

	/* get registers saved in the stub */
	ldr	r2, [r1, #S_R0]		@ r0
	ldr	r3, [r1, #S_R1]		@ r1
	ldr	r4, [r1, #S_PC] 	@ lr_<exception> (parent PC)
	ldr	r5, [r1, #S_PSR]	@ spsr_<exception> (parent CPSR)

	/* fix r0 and r1 */
	str	r2, [sp, #S_R0]
	str	r3, [sp, #S_R1]

	/* store sp_svc, if we were in usr mode we'll fix this later */
	add	r6, sp, #S_FRAME_SIZE
	addne	r6, #4			@ stack wasn't aligned
	str	r6, [sp, #S_SP]

	str	lr, [sp, #S_LR]		@ store lr_svc, fix later for usr mode
	str	r4, [sp, #S_PC]		@ store lr_<exception>
	str	r5, [sp, #S_PSR]	@ store spsr_<exception>

	/* set ORIG_r0 */
	mov	r2, #-1
	str	r2, [sp, #S_OLD_R0]

	/* if we were in usr mode then we need sp_usr and lr_usr instead */
	and	r1, r5, #MODE_MASK
	cmp	r1, #USR_MODE
	bne	1f
	add	r1, sp, #S_SP
	stmia	r1, { sp,lr }^

	/* Call the handler. r0 is the vector number, r1 := pt_regs */
1:	mov	r1, sp
	bl	do_handle_exception

	/*
	 * make sure we restore sp_svc on mode change. No need to
	 * worry about lr_svc though, as that gets clobbered on
	 * exception entry anyway.
	 */
	str	r6, [sp, #S_SP]

	/* return from exception */
	msr	spsr_cxsf, r5
	ldmia	sp, { r0-pc }^

.align 5
vector_addrexcptn:
	b	vector_addrexcptn

.section .text.ex
.align 5
vector_table:
	b	vector_rst
	b	vector_und
	b	vector_svc
	b	vector_pabt
	b	vector_dabt
	b	vector_addrexcptn	@ should never happen
	b	vector_irq
	b	vector_fiq
