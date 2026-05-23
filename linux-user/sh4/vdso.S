/*
 * sh4 linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include "vdso-asmoffset.h"

	.text

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

/*
 * SH4 syscall convention:
 *   Syscall number in r3 (caller-saved, so no save/restore needed)
 *   Arguments in r4-r7
 *   Return value in r0
 *   Syscall instruction: trapa #0x10
 */

.macro vdso_syscall name, nr
\name:
	.cfi_startproc
	mov.l	1f, r3
	trapa	#0x10
	rts
	nop
	.align	2
1:	.long	\nr
	.cfi_endproc
endf \name
.endm

vdso_syscall __vdso_clock_gettime, __NR_clock_gettime
vdso_syscall __vdso_clock_gettime64, __NR_clock_gettime64
vdso_syscall __vdso_clock_getres, __NR_clock_getres
vdso_syscall __vdso_gettimeofday, __NR_gettimeofday

/*
 * Signal return trampolines.
 *
 * For sigreturn: r15 points to struct sigframe; sigcontext is at
 *   offset SIGFRAME_SC_OFFSET (0).
 * For rt_sigreturn: r15 points to struct rt_sigframe; sigcontext is at
 *   offset RT_SIGFRAME_SC_OFFSET (148).
 *
 * A single CFI region covers both trampolines.  The CFA is set to the
 * start of the relevant sigcontext; all register offsets are then
 * identical for both trampolines.  Between the two trampolines we use
 * .cfi_def_cfa_offset to update the CFA base for the different layout.
 */

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */
	.cfi_startproc simple
	.cfi_signal_frame
	.cfi_return_column 16		/* return column is PC */

	/* CFA = r15 + SIGFRAME_SC_OFFSET = r15 (sigcontext base, sigreturn). */
	.cfi_def_cfa		15, SIGFRAME_SC_OFFSET

	/* Integer registers r0-r15: sc_gregs[n] at sigcontext + SC_GREGS + n*4. */
	.cfi_offset		0,  SC_GREGS + 0  * 4
	.cfi_offset		1,  SC_GREGS + 1  * 4
	.cfi_offset		2,  SC_GREGS + 2  * 4
	.cfi_offset		3,  SC_GREGS + 3  * 4
	.cfi_offset		4,  SC_GREGS + 4  * 4
	.cfi_offset		5,  SC_GREGS + 5  * 4
	.cfi_offset		6,  SC_GREGS + 6  * 4
	.cfi_offset		7,  SC_GREGS + 7  * 4
	.cfi_offset		8,  SC_GREGS + 8  * 4
	.cfi_offset		9,  SC_GREGS + 9  * 4
	.cfi_offset		10, SC_GREGS + 10 * 4
	.cfi_offset		11, SC_GREGS + 11 * 4
	.cfi_offset		12, SC_GREGS + 12 * 4
	.cfi_offset		13, SC_GREGS + 13 * 4
	.cfi_offset		14, SC_GREGS + 14 * 4
	.cfi_offset		15, SC_GREGS + 15 * 4

	/* PC (return column). */
	.cfi_offset		16, SC_PC

	/* Control registers. */
	.cfi_offset		17, SC_PR
	.cfi_offset		18, SC_GBR
	.cfi_offset		20, SC_MACH
	.cfi_offset		21, SC_MACL

	/* FP registers fr0-fr15: sc_fpregs[n] at sigcontext + SC_FPREGS + n*4. */
	.cfi_offset		25, SC_FPREGS + 0  * 4
	.cfi_offset		26, SC_FPREGS + 1  * 4
	.cfi_offset		27, SC_FPREGS + 2  * 4
	.cfi_offset		28, SC_FPREGS + 3  * 4
	.cfi_offset		29, SC_FPREGS + 4  * 4
	.cfi_offset		30, SC_FPREGS + 5  * 4
	.cfi_offset		31, SC_FPREGS + 6  * 4
	.cfi_offset		32, SC_FPREGS + 7  * 4
	.cfi_offset		33, SC_FPREGS + 8  * 4
	.cfi_offset		34, SC_FPREGS + 9  * 4
	.cfi_offset		35, SC_FPREGS + 10 * 4
	.cfi_offset		36, SC_FPREGS + 11 * 4
	.cfi_offset		37, SC_FPREGS + 12 * 4
	.cfi_offset		38, SC_FPREGS + 13 * 4
	.cfi_offset		39, SC_FPREGS + 14 * 4
	.cfi_offset		40, SC_FPREGS + 15 * 4

	/* FPUL, FPSCR. */
	.cfi_offset		23, SC_FPUL
	.cfi_offset		24, SC_FPSCR

	nop

sigreturn_region_start:
__kernel_sigreturn:
	mov.l	1f, r3
	trapa	#0x10
	.align	2
1:	.long	__NR_sigreturn
endf __kernel_sigreturn

	/* Update CFA base for the rt_sigreturn frame layout. */
	.cfi_def_cfa_offset	RT_SIGFRAME_SC_OFFSET

__kernel_rt_sigreturn:
	mov.l	2f, r3
	trapa	#0x10
	.align	2
2:	.long	__NR_rt_sigreturn
endf __kernel_rt_sigreturn
sigreturn_region_end:

	.cfi_endproc
