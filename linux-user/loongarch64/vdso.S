/*
 * Loongarch64 linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include <asm/errno.h>
#include "vdso-asmoffset.h"


	.text

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

.macro vdso_syscall name, nr
\name:
	li.w	$a7, \nr
	syscall	0
	jr	$ra
endf	\name
.endm

	.cfi_startproc

vdso_syscall __vdso_gettimeofday, __NR_gettimeofday
vdso_syscall __vdso_clock_gettime, __NR_clock_gettime
vdso_syscall __vdso_clock_getres, __NR_clock_getres
vdso_syscall __vdso_getcpu, __NR_getcpu

	.cfi_endproc

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */

	.cfi_startproc simple
	.cfi_signal_frame

#define B_GR	offsetof_sigcontext_gr
#define B_FR	sizeof_sigcontext + sizeof_sctx_info + offsetof_fpucontext_fr

	.cfi_def_cfa	2, offsetof_sigcontext

	/* Return address */
	.cfi_return_column 64
	.cfi_offset	64, offsetof_sigcontext_pc	/* pc */

	/* Integer registers */
	.cfi_offset	1, B_GR + 1 * 8
	.cfi_offset	2, B_GR + 2 * 8
	.cfi_offset	3, B_GR + 3 * 8
	.cfi_offset	4, B_GR + 4 * 8
	.cfi_offset	5, B_GR + 5 * 8
	.cfi_offset	6, B_GR + 6 * 8
	.cfi_offset	7, B_GR + 7 * 8
	.cfi_offset	8, B_GR + 8 * 8
	.cfi_offset	9, B_GR + 9 * 8
	.cfi_offset	10, B_GR + 10 * 8
	.cfi_offset	11, B_GR + 11 * 8
	.cfi_offset	12, B_GR + 12 * 8
	.cfi_offset	13, B_GR + 13 * 8
	.cfi_offset	14, B_GR + 14 * 8
	.cfi_offset	15, B_GR + 15 * 8
	.cfi_offset	16, B_GR + 16 * 8
	.cfi_offset	17, B_GR + 17 * 8
	.cfi_offset	18, B_GR + 18 * 8
	.cfi_offset	19, B_GR + 19 * 8
	.cfi_offset	20, B_GR + 20 * 8
	.cfi_offset	21, B_GR + 21 * 8
	.cfi_offset	22, B_GR + 22 * 8
	.cfi_offset	23, B_GR + 23 * 8
	.cfi_offset	24, B_GR + 24 * 8
	.cfi_offset	25, B_GR + 25 * 8
	.cfi_offset	26, B_GR + 26 * 8
	.cfi_offset	27, B_GR + 27 * 8
	.cfi_offset	28, B_GR + 28 * 8
	.cfi_offset	29, B_GR + 29 * 8
	.cfi_offset	30, B_GR + 30 * 8
	.cfi_offset	31, B_GR + 31 * 8

	/* Floating point registers */
	.cfi_offset	32, B_FR + 0
	.cfi_offset	33, B_FR + 1 * 8
	.cfi_offset	34, B_FR + 2 * 8
	.cfi_offset	35, B_FR + 3 * 8
	.cfi_offset	36, B_FR + 4 * 8
	.cfi_offset	37, B_FR + 5 * 8
	.cfi_offset	38, B_FR + 6 * 8
	.cfi_offset	39, B_FR + 7 * 8
	.cfi_offset	40, B_FR + 8 * 8
	.cfi_offset	41, B_FR + 9 * 8
	.cfi_offset	42, B_FR + 10 * 8
	.cfi_offset	43, B_FR + 11 * 8
	.cfi_offset	44, B_FR + 12 * 8
	.cfi_offset	45, B_FR + 13 * 8
	.cfi_offset	46, B_FR + 14 * 8
	.cfi_offset	47, B_FR + 15 * 8
	.cfi_offset	48, B_FR + 16 * 8
	.cfi_offset	49, B_FR + 17 * 8
	.cfi_offset	50, B_FR + 18 * 8
	.cfi_offset	51, B_FR + 19 * 8
	.cfi_offset	52, B_FR + 20 * 8
	.cfi_offset	53, B_FR + 21 * 8
	.cfi_offset	54, B_FR + 22 * 8
	.cfi_offset	55, B_FR + 23 * 8
	.cfi_offset	56, B_FR + 24 * 8
	.cfi_offset	57, B_FR + 25 * 8
	.cfi_offset	58, B_FR + 26 * 8
	.cfi_offset	59, B_FR + 27 * 8
	.cfi_offset	60, B_FR + 28 * 8
	.cfi_offset	61, B_FR + 29 * 8
	.cfi_offset	62, B_FR + 30 * 8
	.cfi_offset	63, B_FR + 31 * 8

	nop

__vdso_rt_sigreturn:
	li.w	$a7, __NR_rt_sigreturn
sigreturn_region_start:
	syscall	0
sigreturn_region_end:
	.cfi_endproc
endf __vdso_rt_sigreturn
