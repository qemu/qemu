/*
 * i386 linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include "vdso-asmoffset.h"

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

.macro vdso_syscall1 name, nr
\name:
	.cfi_startproc
	mov	%ebx, %edx
	.cfi_register %ebx, %edx
	mov	4(%esp), %ebx
	mov	$\nr, %eax
	int	$0x80
	mov	%edx, %ebx
	ret
	.cfi_endproc
endf	\name
.endm

.macro vdso_syscall2 name, nr
\name:
	.cfi_startproc
	mov	%ebx, %edx
	.cfi_register %ebx, %edx
	mov	4(%esp), %ebx
	mov	8(%esp), %ecx
	mov	$\nr, %eax
	int	$0x80
	mov	%edx, %ebx
	ret
	.cfi_endproc
endf	\name
.endm

.macro vdso_syscall3 name, nr
\name:
	.cfi_startproc
	push	%ebx
	.cfi_adjust_cfa_offset 4
	.cfi_rel_offset %ebx, 0
	mov	8(%esp), %ebx
	mov	12(%esp), %ecx
	mov	16(%esp), %edx
	mov	$\nr, %eax
	int	$0x80
	pop	%ebx
	.cfi_adjust_cfa_offset -4
	.cfi_restore %ebx
	ret
	.cfi_endproc
endf	\name
.endm

__kernel_vsyscall:
	.cfi_startproc
	int	$0x80
	ret
	.cfi_endproc
endf	__kernel_vsyscall

vdso_syscall2 __vdso_clock_gettime, __NR_clock_gettime
vdso_syscall2 __vdso_clock_gettime64, __NR_clock_gettime64
vdso_syscall2 __vdso_clock_getres, __NR_clock_getres
vdso_syscall2 __vdso_gettimeofday, __NR_gettimeofday
vdso_syscall1 __vdso_time, __NR_time
vdso_syscall3 __vdso_getcpu, __NR_gettimeofday

/*
 * Signal return handlers.
 */

	.cfi_startproc simple
	.cfi_signal_frame

/*
 * For convenience, put the cfa just above eip in sigcontext, and count
 * offsets backward from there.  Re-compute the cfa in the two contexts
 * we have for signal unwinding.  This is far simpler than the
 * DW_CFA_expression form that the kernel uses, and is equally correct.
 */

	.cfi_def_cfa	%esp, SIGFRAME_SIGCONTEXT_eip + 4

	.cfi_offset	%eip, -4
			/* err, -8 */
			/* trapno, -12 */
	.cfi_offset	%eax, -16
	.cfi_offset	%ecx, -20
	.cfi_offset	%edx, -24
	.cfi_offset	%ebx, -28
	.cfi_offset	%esp, -32
	.cfi_offset	%ebp, -36
	.cfi_offset	%esi, -40
	.cfi_offset	%edi, -44

/*
 * While this frame is marked as a signal frame, that only applies to how
 * the return address is handled for the outer frame.  The return address
 * that arrived here, from the inner frame, is not marked as a signal frame
 * and so the unwinder still tries to subtract 1 to examine the presumed
 * call insn.  Thus we must extend the unwind info to a nop before the start.
 */
	nop

__kernel_sigreturn:
	popl	%eax	/* pop sig */
	.cfi_adjust_cfa_offset -4
	movl	$__NR_sigreturn, %eax
	int	$0x80
endf	__kernel_sigreturn

	.cfi_def_cfa_offset RT_SIGFRAME_SIGCONTEXT_eip + 4
	nop

__kernel_rt_sigreturn:
	movl	$__NR_rt_sigreturn, %eax
	int	$0x80
endf	__kernel_rt_sigreturn

	.cfi_endproc

/*
 * TODO: Add elf notes.  E.g.
 *
 * #include <linux/elfnote.h>
 * ELFNOTE_START(Linux, 0, "a")
 *   .long LINUX_VERSION_CODE
 * ELFNOTE_END
 *
 * but what version number would we set for QEMU?
 */
