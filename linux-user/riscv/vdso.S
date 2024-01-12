/*
 * RISC-V linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include <asm/errno.h>

#if __riscv_xlen == 32
# define TARGET_ABI32
#endif
#include "vdso-asmoffset.h"

	.text

.macro endf name
	.globl	\name
	.type	\name, @function
	.size	\name, . - \name
.endm

.macro raw_syscall nr
	li	a7, \nr
	ecall
.endm

.macro vdso_syscall name, nr
\name:
	raw_syscall \nr
	ret
endf	\name
.endm

__vdso_gettimeofday:
	.cfi_startproc
#ifdef __NR_gettimeofday
	raw_syscall __NR_gettimeofday
	ret
#else
	/* No gettimeofday, fall back to clock_gettime64. */
	beq	a1, zero, 1f
	sw	zero, 0(a1)	/* tz->tz_minuteswest = 0 */
	sw	zero, 4(a1)	/* tz->tz_dsttime = 0 */
1:	addi	sp, sp, -32
	.cfi_adjust_cfa_offset 32
	sw	a0, 16(sp)	/* save tv */
	mv	a0, sp
	raw_syscall __NR_clock_gettime64
	lw	t0, 0(sp)	/* timespec.tv_sec.low */
	lw	t1, 4(sp)	/* timespec.tv_sec.high */
	lw	t2, 8(sp)	/* timespec.tv_nsec.low */
	lw	a1, 16(sp)	/* restore tv */
	addi	sp, sp, 32
	.cfi_adjust_cfa_offset -32
	bne	a0, zero, 9f	/* syscall error? */
	li	a0, -EOVERFLOW
	bne	t1, zero, 9f	/* y2038? */
	li	a0, 0
	li	t3, 1000
	divu	t2, t2, t3	/* nsec -> usec */
	sw	t0, 0(a1)	/* tz->tv_sec */
	sw	t2, 4(a1)	/* tz->tv_usec */
9:	ret
#endif
	.cfi_endproc
endf __vdso_gettimeofday

	.cfi_startproc

#ifdef __NR_clock_gettime
vdso_syscall __vdso_clock_gettime, __NR_clock_gettime
#else
vdso_syscall __vdso_clock_gettime, __NR_clock_gettime64
#endif

#ifdef __NR_clock_getres
vdso_syscall __vdso_clock_getres, __NR_clock_getres
#else
vdso_syscall __vdso_clock_getres, __NR_clock_getres_time64
#endif

vdso_syscall __vdso_getcpu, __NR_getcpu

__vdso_flush_icache:
	/* qemu does not need to flush the icache */
	li	a0, 0
	ret
endf __vdso_flush_icache

	.cfi_endproc

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */

	.cfi_startproc simple
	.cfi_signal_frame

#define sizeof_reg	(__riscv_xlen / 8)
#define sizeof_freg	8
#define B_GR	0
#define B_FR	offsetof_freg0

	.cfi_def_cfa	2, offsetof_uc_mcontext

	/* Return address */
	.cfi_return_column 64
	.cfi_offset	64, B_GR + 0			/* pc */

	/* Integer registers */
	.cfi_offset	1, B_GR + 1 * sizeof_reg	/* r1 (ra) */
	.cfi_offset	2, B_GR + 2 * sizeof_reg	/* r2 (sp) */
	.cfi_offset	3, B_GR + 3 * sizeof_reg
	.cfi_offset	4, B_GR + 4 * sizeof_reg
	.cfi_offset	5, B_GR + 5 * sizeof_reg
	.cfi_offset	6, B_GR + 6 * sizeof_reg
	.cfi_offset	7, B_GR + 7 * sizeof_reg
	.cfi_offset	8, B_GR + 8 * sizeof_reg
	.cfi_offset	9, B_GR + 9 * sizeof_reg
	.cfi_offset	10, B_GR + 10 * sizeof_reg
	.cfi_offset	11, B_GR + 11 * sizeof_reg
	.cfi_offset	12, B_GR + 12 * sizeof_reg
	.cfi_offset	13, B_GR + 13 * sizeof_reg
	.cfi_offset	14, B_GR + 14 * sizeof_reg
	.cfi_offset	15, B_GR + 15 * sizeof_reg
	.cfi_offset	16, B_GR + 16 * sizeof_reg
	.cfi_offset	17, B_GR + 17 * sizeof_reg
	.cfi_offset	18, B_GR + 18 * sizeof_reg
	.cfi_offset	19, B_GR + 19 * sizeof_reg
	.cfi_offset	20, B_GR + 20 * sizeof_reg
	.cfi_offset	21, B_GR + 21 * sizeof_reg
	.cfi_offset	22, B_GR + 22 * sizeof_reg
	.cfi_offset	23, B_GR + 23 * sizeof_reg
	.cfi_offset	24, B_GR + 24 * sizeof_reg
	.cfi_offset	25, B_GR + 25 * sizeof_reg
	.cfi_offset	26, B_GR + 26 * sizeof_reg
	.cfi_offset	27, B_GR + 27 * sizeof_reg
	.cfi_offset	28, B_GR + 28 * sizeof_reg
	.cfi_offset	29, B_GR + 29 * sizeof_reg
	.cfi_offset	30, B_GR + 30 * sizeof_reg
	.cfi_offset	31, B_GR + 31 * sizeof_reg	/* r31 */

	.cfi_offset	32, B_FR + 0			/* f0 */
	.cfi_offset	33, B_FR + 1 * sizeof_freg	/* f1 */
	.cfi_offset	34, B_FR + 2 * sizeof_freg
	.cfi_offset	35, B_FR + 3 * sizeof_freg
	.cfi_offset	36, B_FR + 4 * sizeof_freg
	.cfi_offset	37, B_FR + 5 * sizeof_freg
	.cfi_offset	38, B_FR + 6 * sizeof_freg
	.cfi_offset	39, B_FR + 7 * sizeof_freg
	.cfi_offset	40, B_FR + 8 * sizeof_freg
	.cfi_offset	41, B_FR + 9 * sizeof_freg
	.cfi_offset	42, B_FR + 10 * sizeof_freg
	.cfi_offset	43, B_FR + 11 * sizeof_freg
	.cfi_offset	44, B_FR + 12 * sizeof_freg
	.cfi_offset	45, B_FR + 13 * sizeof_freg
	.cfi_offset	46, B_FR + 14 * sizeof_freg
	.cfi_offset	47, B_FR + 15 * sizeof_freg
	.cfi_offset	48, B_FR + 16 * sizeof_freg
	.cfi_offset	49, B_FR + 17 * sizeof_freg
	.cfi_offset	50, B_FR + 18 * sizeof_freg
	.cfi_offset	51, B_FR + 19 * sizeof_freg
	.cfi_offset	52, B_FR + 20 * sizeof_freg
	.cfi_offset	53, B_FR + 21 * sizeof_freg
	.cfi_offset	54, B_FR + 22 * sizeof_freg
	.cfi_offset	55, B_FR + 23 * sizeof_freg
	.cfi_offset	56, B_FR + 24 * sizeof_freg
	.cfi_offset	57, B_FR + 25 * sizeof_freg
	.cfi_offset	58, B_FR + 26 * sizeof_freg
	.cfi_offset	59, B_FR + 27 * sizeof_freg
	.cfi_offset	60, B_FR + 28 * sizeof_freg
	.cfi_offset	61, B_FR + 29 * sizeof_freg
	.cfi_offset	62, B_FR + 30 * sizeof_freg
	.cfi_offset	63, B_FR + 31 * sizeof_freg	/* f31 */

	nop

__vdso_rt_sigreturn:
	raw_syscall __NR_rt_sigreturn
endf __vdso_rt_sigreturn

	.cfi_endproc
