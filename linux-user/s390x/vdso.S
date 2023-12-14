/*
 * s390x linux replacement vdso.
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

.macro raw_syscall n
        .ifne	\n < 0x100
	svc	\n
	.else
	lghi	%r1, \n
	svc	0
        .endif
.endm

.macro vdso_syscall name, nr
\name:
	.cfi_startproc
	aghi	%r15, -(STACK_FRAME_OVERHEAD + 16)
	.cfi_adjust_cfa_offset STACK_FRAME_OVERHEAD + 16
	stg	%r14, STACK_FRAME_OVERHEAD(%r15)
	.cfi_rel_offset %r14, STACK_FRAME_OVERHEAD
	raw_syscall \nr
	lg	%r14, STACK_FRAME_OVERHEAD(%r15)
	aghi	%r15, STACK_FRAME_OVERHEAD + 16
	.cfi_restore %r14
	.cfi_adjust_cfa_offset -(STACK_FRAME_OVERHEAD + 16)
	br	%r14
	.cfi_endproc
endf	\name
.endm

vdso_syscall __kernel_gettimeofday, __NR_gettimeofday
vdso_syscall __kernel_clock_gettime, __NR_clock_gettime
vdso_syscall __kernel_clock_getres, __NR_clock_getres
vdso_syscall __kernel_getcpu, __NR_getcpu

/*
 * TODO unwind info, though we're ok without it.
 * The kernel supplies bogus empty unwind info, and it is likely ignored
 * by all users.  Without it we get the fallback signal frame handling.
 */

__kernel_sigreturn:
	raw_syscall __NR_sigreturn
endf	__kernel_sigreturn

__kernel_rt_sigreturn:
	raw_syscall __NR_rt_sigreturn
endf	__kernel_rt_sigreturn
