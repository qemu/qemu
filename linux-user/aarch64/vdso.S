/*
 * aarch64 linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

/* ??? These are in include/elf.h, which is not ready for inclusion in asm. */
#define NT_GNU_PROPERTY_TYPE_0  5
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND      0xc0000000
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI      (1U << 0)
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC      (1U << 1)

#define GNU_PROPERTY_AARCH64_FEATURE_1_DEFAULT \
    (GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC)

	.section .note.gnu.property
	.align	3
	.long	2f - 1f
	.long	6f - 3f
	.long	NT_GNU_PROPERTY_TYPE_0
1:	.string	"GNU"
2:	.align	3
3:	.long	GNU_PROPERTY_AARCH64_FEATURE_1_AND
	.long	5f - 4f
4:	.long	GNU_PROPERTY_AARCH64_FEATURE_1_DEFAULT
5:	.align	3
6:

	.text

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

.macro vdso_syscall name, nr
\name:
	bti	c
	mov	x8, #\nr
	svc	#0
	ret
endf	\name
.endm

	.cfi_startproc

vdso_syscall __kernel_gettimeofday, __NR_gettimeofday
vdso_syscall __kernel_clock_gettime, __NR_clock_gettime
vdso_syscall __kernel_clock_getres, __NR_clock_getres

	.cfi_endproc


/*
 * TODO: The kernel makes a big deal of turning off the .cfi directives,
 * because they cause libgcc to crash, but that's because they're wrong.
 *
 * For now, elide the unwind info for __kernel_rt_sigreturn and rely on
 * the libgcc fallback routine as we have always done.  This requires
 * that the code sequence used be exact.
 *
 * Add a nop as a spacer to ensure that unwind does not pick up the
 * unwind info from the preceding syscall.
 */
	nop
__kernel_rt_sigreturn:
	/* No BTI C insn here -- we arrive via RET. */
	mov	x8, #__NR_rt_sigreturn
sigreturn_region_start:
	svc	#0
sigreturn_region_end:
endf	__kernel_rt_sigreturn
