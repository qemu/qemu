/*
 * arm linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include "vdso-asmoffset.h"

/*
 * All supported cpus have T16 instructions: at least arm4t.
 *
 * We support user-user with m-profile cpus as an extension, because it
 * is useful for testing gcc, which requires we avoid A32 instructions.
 */
	.thumb
	.arch	armv4t
	.eabi_attribute Tag_FP_arch, 0
	.eabi_attribute Tag_ARM_ISA_use, 0

	.text

.macro	raw_syscall n
	.ifne \n < 0x100
	mov	r7, #\n
	.elseif \n < 0x1ff
	mov	r7, #0xff
	add	r7, #(\n - 0xff)
	.else
	.err
	.endif
	swi	#0
.endm

.macro	fdpic_thunk ofs
	ldr	r3, [sp, #\ofs]
	ldmia	r2, {r2, r3}
	mov	r9, r3
	bx	r2
.endm

.macro	endf name
	.globl	\name
	.type	\name, %function
	.size	\name, . - \name
.endm

/*
 * We must save/restore r7 for the EABI syscall number.
 * While we're doing that, we might as well save LR to get a free return,
 * and a branch that is interworking back to ARMv5.
 */

.macro SYSCALL name, nr
\name:
	.cfi_startproc
	push	{r7, lr}
	.cfi_adjust_cfa_offset 8
	.cfi_offset r7, -8
	.cfi_offset lr, -4
	raw_syscall \nr
	pop	{r7, pc}
	.cfi_endproc
endf \name
.endm

SYSCALL	__vdso_clock_gettime, __NR_clock_gettime
SYSCALL __vdso_clock_gettime64, __NR_clock_gettime64
SYSCALL __vdso_clock_getres, __NR_clock_getres
SYSCALL __vdso_gettimeofday, __NR_gettimeofday


/*
 * We, like the real kernel, use a table of sigreturn trampolines.
 * Unlike the real kernel, we do not attempt to pack this into as
 * few bytes as possible -- simply use 8 bytes per slot.
 *
 * Within each slot, use the exact same code sequence as the kernel,
 * lest we trip up someone doing code inspection.
 */

.macro	slot n
	.balign	8
	.org	sigreturn_codes + 8 * \n
.endm

.macro cfi_fdpic_r9 ofs
	/*
	 * fd = *(r13 + ofs)
         * r9 = *(fd + 4)
	 *
	 * DW_CFA_expression r9, length (7),
	 *   DW_OP_breg13, ofs, DW_OP_deref,
	 *   DW_OP_plus_uconst, 4, DW_OP_deref
         */
	.cfi_escape 0x10, 9, 7, 0x7d, (\ofs & 0x7f) + 0x80, (\ofs >> 7), 0x06, 0x23, 4, 0x06
.endm

.macro cfi_fdpic_pc ofs
	/*
	 * fd = *(r13 + ofs)
         * pc = *fd
	 *
	 * DW_CFA_expression lr (14), length (5),
	 *   DW_OP_breg13, ofs, DW_OP_deref, DW_OP_deref
         */
	.cfi_escape 0x10, 14, 5, 0x7d, (\ofs & 0x7f) + 0x80, (\ofs >> 7), 0x06, 0x06
.endm

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */
	.cfi_startproc simple
	.cfi_signal_frame
	.cfi_return_column 15

	.cfi_def_cfa	sp, 32 + 64
	.cfi_offset	r0, -16 * 4
	.cfi_offset	r1, -15 * 4
	.cfi_offset	r2, -14 * 4
	.cfi_offset	r3, -13 * 4
	.cfi_offset	r4, -12 * 4
	.cfi_offset	r5, -11 * 4
	.cfi_offset	r6, -10 * 4
	.cfi_offset	r7, -9 * 4
	.cfi_offset	r8, -8 * 4
	.cfi_offset	r9, -7 * 4
	.cfi_offset	r10, -6 * 4
	.cfi_offset	r11, -5 * 4
	.cfi_offset	r12, -4 * 4
	.cfi_offset	r13, -3 * 4
	.cfi_offset	r14, -2 * 4
	.cfi_offset	r15, -1 * 4

	nop

	.balign	16
sigreturn_codes:
sigreturn_region_start:
	/* [EO]ABI sigreturn */
	slot	0
	raw_syscall __NR_sigreturn

	.cfi_def_cfa_offset 160 + 64

	/* [EO]ABI rt_sigreturn */
	slot	1
	raw_syscall __NR_rt_sigreturn

	.cfi_endproc

	/* FDPIC sigreturn */
	.cfi_startproc
	cfi_fdpic_pc SIGFRAME_RC3_OFFSET
	cfi_fdpic_r9 SIGFRAME_RC3_OFFSET

	slot	2
	fdpic_thunk SIGFRAME_RC3_OFFSET
	.cfi_endproc

	/* FDPIC rt_sigreturn */
	.cfi_startproc
	cfi_fdpic_pc RT_SIGFRAME_RC3_OFFSET
	cfi_fdpic_r9 RT_SIGFRAME_RC3_OFFSET

	slot	3
	fdpic_thunk RT_SIGFRAME_RC3_OFFSET
	.cfi_endproc

	.balign	16
endf sigreturn_codes
sigreturn_region_end:
