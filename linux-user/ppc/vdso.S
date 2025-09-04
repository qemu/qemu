/*
 * PowerPC linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include <asm/errno.h>

#ifndef _ARCH_PPC64
# define TARGET_ABI32
#endif
#include "vdso-asmoffset.h"


	.text

.macro endf name
	.globl	\name
	.size	\name, .-\name
	/* For PPC64, functions have special linkage; we export pointers. */
#ifndef _ARCH_PPC64
	.type	\name, @function
#endif
.endm

.macro raw_syscall nr
	addi	0, 0, \nr
	sc
.endm

.macro vdso_syscall name, nr
\name:
	raw_syscall \nr
	blr
endf	\name
.endm

	.cfi_startproc

vdso_syscall __kernel_gettimeofday, __NR_gettimeofday
vdso_syscall __kernel_clock_gettime, __NR_clock_gettime
vdso_syscall __kernel_clock_getres, __NR_clock_getres
vdso_syscall __kernel_getcpu, __NR_getcpu
vdso_syscall __kernel_time, __NR_time

#ifdef __NR_clock_gettime64
vdso_syscall __kernel_clock_gettime64, __NR_clock_gettime64
#endif

__kernel_sync_dicache:
	/* qemu does not need to flush caches */
	blr
endf	__kernel_sync_dicache

	.cfi_endproc

/*
 * TODO: __kernel_get_tbfreq
 * This is probably a constant for QEMU.
 */

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */

	.cfi_startproc simple
	.cfi_signal_frame

#ifdef _ARCH_PPC64
# define __kernel_sigtramp_rt  __kernel_sigtramp_rt64
# define sizeof_reg	8
#else
# define __kernel_sigtramp_rt  __kernel_sigtramp_rt32
# define sizeof_reg	4
#endif
#define sizeof_freg	8
#define sizeof_vreg	16

	.cfi_def_cfa	1, SIGNAL_FRAMESIZE + offsetof_rt_sigframe_mcontext

	/* Return address */
	.cfi_return_column 67
	.cfi_offset	67, 32 * sizeof_reg		/* nip */

	/* Integer registers */
	.cfi_offset	0, 0 * sizeof_reg
	.cfi_offset	1, 1 * sizeof_reg
	.cfi_offset	2, 2 * sizeof_reg
	.cfi_offset	3, 3 * sizeof_reg
	.cfi_offset	4, 4 * sizeof_reg
	.cfi_offset	5, 5 * sizeof_reg
	.cfi_offset	6, 6 * sizeof_reg
	.cfi_offset	7, 7 * sizeof_reg
	.cfi_offset	8, 8 * sizeof_reg
	.cfi_offset	9, 9 * sizeof_reg
	.cfi_offset	10, 10 * sizeof_reg
	.cfi_offset	11, 11 * sizeof_reg
	.cfi_offset	12, 12 * sizeof_reg
	.cfi_offset	13, 13 * sizeof_reg
	.cfi_offset	14, 14 * sizeof_reg
	.cfi_offset	15, 15 * sizeof_reg
	.cfi_offset	16, 16 * sizeof_reg
	.cfi_offset	17, 17 * sizeof_reg
	.cfi_offset	18, 18 * sizeof_reg
	.cfi_offset	19, 19 * sizeof_reg
	.cfi_offset	20, 20 * sizeof_reg
	.cfi_offset	21, 21 * sizeof_reg
	.cfi_offset	22, 22 * sizeof_reg
	.cfi_offset	23, 23 * sizeof_reg
	.cfi_offset	24, 24 * sizeof_reg
	.cfi_offset	25, 25 * sizeof_reg
	.cfi_offset	26, 26 * sizeof_reg
	.cfi_offset	27, 27 * sizeof_reg
	.cfi_offset	28, 28 * sizeof_reg
	.cfi_offset	29, 29 * sizeof_reg
	.cfi_offset	30, 30 * sizeof_reg
	.cfi_offset	31, 31 * sizeof_reg
	.cfi_offset	65, 36 * sizeof_reg		/* lr */
	.cfi_offset	70, 38 * sizeof_reg		/* ccr */

	/* Floating point registers */
	.cfi_offset	32, offsetof_mcontext_fregs
	.cfi_offset	33, offsetof_mcontext_fregs + 1 * sizeof_freg
	.cfi_offset	34, offsetof_mcontext_fregs + 2 * sizeof_freg
	.cfi_offset	35, offsetof_mcontext_fregs + 3 * sizeof_freg
	.cfi_offset	36, offsetof_mcontext_fregs + 4 * sizeof_freg
	.cfi_offset	37, offsetof_mcontext_fregs + 5 * sizeof_freg
	.cfi_offset	38, offsetof_mcontext_fregs + 6 * sizeof_freg
	.cfi_offset	39, offsetof_mcontext_fregs + 7 * sizeof_freg
	.cfi_offset	40, offsetof_mcontext_fregs + 8 * sizeof_freg
	.cfi_offset	41, offsetof_mcontext_fregs + 9 * sizeof_freg
	.cfi_offset	42, offsetof_mcontext_fregs + 10 * sizeof_freg
	.cfi_offset	43, offsetof_mcontext_fregs + 11 * sizeof_freg
	.cfi_offset	44, offsetof_mcontext_fregs + 12 * sizeof_freg
	.cfi_offset	45, offsetof_mcontext_fregs + 13 * sizeof_freg
	.cfi_offset	46, offsetof_mcontext_fregs + 14 * sizeof_freg
	.cfi_offset	47, offsetof_mcontext_fregs + 15 * sizeof_freg
	.cfi_offset	48, offsetof_mcontext_fregs + 16 * sizeof_freg
	.cfi_offset	49, offsetof_mcontext_fregs + 17 * sizeof_freg
	.cfi_offset	50, offsetof_mcontext_fregs + 18 * sizeof_freg
	.cfi_offset	51, offsetof_mcontext_fregs + 19 * sizeof_freg
	.cfi_offset	52, offsetof_mcontext_fregs + 20 * sizeof_freg
	.cfi_offset	53, offsetof_mcontext_fregs + 21 * sizeof_freg
	.cfi_offset	54, offsetof_mcontext_fregs + 22 * sizeof_freg
	.cfi_offset	55, offsetof_mcontext_fregs + 23 * sizeof_freg
	.cfi_offset	56, offsetof_mcontext_fregs + 24 * sizeof_freg
	.cfi_offset	57, offsetof_mcontext_fregs + 25 * sizeof_freg
	.cfi_offset	58, offsetof_mcontext_fregs + 26 * sizeof_freg
	.cfi_offset	59, offsetof_mcontext_fregs + 27 * sizeof_freg
	.cfi_offset	60, offsetof_mcontext_fregs + 28 * sizeof_freg
	.cfi_offset	61, offsetof_mcontext_fregs + 29 * sizeof_freg
	.cfi_offset	62, offsetof_mcontext_fregs + 30 * sizeof_freg
	.cfi_offset	63, offsetof_mcontext_fregs + 31 * sizeof_freg

	/*
	 * Unlike the kernel, unconditionally represent the Altivec/VSX regs.
	 * The space within the stack frame is always available, and most of
	 * our supported processors have them enabled.  The only complication
	 * for PPC64 is the misalignment, so that we have to use indirection.
	 */
.macro	save_vreg_ofs reg, ofs
#ifdef _ARCH_PPC64
	/*
	 * vreg = *(cfa + offsetof(v_regs)) + ofs
         *
         * The CFA is input to the expression on the stack, so:
	 * DW_CFA_expression reg, length (7),
         *   DW_OP_plus_uconst (0x23), vreg_ptr, DW_OP_deref (0x06),
	 *   DW_OP_plus_uconst (0x23), ofs
	 */
	.cfi_escape 0x10, 77 + \reg, 7, 0x23, (offsetof_mcontext_vregs_ptr & 0x7f) + 0x80, offsetof_mcontext_vregs_ptr >> 7, 0x06, 0x23, (\ofs & 0x7f) | 0x80, \ofs >> 7
#else
	.cfi_offset 77 + \reg, offsetof_mcontext_vregs + \ofs
#endif
.endm

.macro	save_vreg reg
	save_vreg_ofs \reg, (\reg * sizeof_vreg)
.endm

	save_vreg   0
	save_vreg   1
	save_vreg   2
	save_vreg   3
	save_vreg   4
	save_vreg   5
	save_vreg   6
	save_vreg   7
	save_vreg   8
	save_vreg   9
	save_vreg  10
	save_vreg  11
	save_vreg  12
	save_vreg  13
	save_vreg  14
	save_vreg  15
	save_vreg  16
	save_vreg  17
	save_vreg  18
	save_vreg  19
	save_vreg  20
	save_vreg  21
	save_vreg  22
	save_vreg  23
	save_vreg  24
	save_vreg  25
	save_vreg  26
	save_vreg  27
	save_vreg  28
	save_vreg  29
	save_vreg  30
	save_vreg  31
	save_vreg  32
	save_vreg_ofs 33, (32 * sizeof_vreg + 12)

	nop

sigreturn_region_start:
__kernel_sigtramp_rt:
	raw_syscall __NR_rt_sigreturn
endf	__kernel_sigtramp_rt

#ifndef _ARCH_PPC64
	/*
	 * The non-rt sigreturn has the same layout at a different offset.
	 * Move the CFA and leave all the other descriptions the same.
	 */
	.cfi_def_cfa	1, SIGNAL_FRAMESIZE + offsetof_sigframe_mcontext
	nop
__kernel_sigtramp32:
	raw_syscall __NR_sigreturn
endf	__kernel_sigtramp32
#endif
sigreturn_region_end:

	.cfi_endproc
