/*
 * x86-64 linux replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

.macro weakalias name
\name	= __vdso_\name
	.weak	\name
.endm

.macro vdso_syscall name, nr
__vdso_\name:
	mov	$\nr, %eax
	syscall
	ret
endf	__vdso_\name
weakalias \name
.endm

	.cfi_startproc

vdso_syscall clock_gettime, __NR_clock_gettime
vdso_syscall clock_getres, __NR_clock_getres
vdso_syscall gettimeofday, __NR_gettimeofday
vdso_syscall time, __NR_time

__vdso_getcpu:
	/*
         * There is no syscall number for this allocated on x64.
	 * We can handle this several ways:
         *
	 * (1) Invent a syscall number for use within qemu.
         *     It should be easy enough to pick a number that
         *     is well out of the way of the kernel numbers.
         *
         * (2) Force the emulated cpu to support the rdtscp insn,
	 *     and initialize the TSC_AUX value the appropriate value.
         *
	 * (3) Pretend that we're always running on cpu 0.
         *
	 * This last is the one that's implemented here, with the
	 * tiny bit of extra code to support rdtscp in place.
         */
	xor	%ecx, %ecx		/* rdtscp w/ tsc_aux = 0 */

	/* if (cpu != NULL) *cpu = (ecx & 0xfff); */
	test	%rdi, %rdi
	jz	1f
	mov	%ecx, %eax
	and	$0xfff, %eax
	mov	%eax, (%rdi)

	/* if (node != NULL) *node = (ecx >> 12); */
1:	test	%rsi, %rsi
	jz	2f
	shr	$12, %ecx
	mov	%ecx, (%rsi)

2:	xor	%eax, %eax
	ret
endf	__vdso_getcpu

weakalias getcpu

	.cfi_endproc

/* TODO: Add elf note for LINUX_VERSION_CODE */
