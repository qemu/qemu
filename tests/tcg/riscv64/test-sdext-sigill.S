/*
 * RISC-V sdext extension "SIGILL" test: check if sdext CSRs
 * and 'dret' will throw SIGILLs outside of Debug Mode.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.equ CSR_DCSR,      0x7b0
.equ CSR_DPC,       0x7b1
.equ CSR_DSCRATCH0, 0x7b2

	.option norvc
	.text

	.global _start

_start:
	la t0, trap_handler
	csrw mtvec, t0

	dret
	j _fail

	li t0, 1
	csrw CSR_DPC, t0
	j _fail

	li t0, 1
	csrw CSR_DSCRATCH0, t0
	j _fail

	li t0, 1
	csrw CSR_DCSR, t0
	j _fail

	li a0, 0
	j _exit

_fail:
	li a0, 1

/* Exit through the semihosting SYS_EXIT_EXTENDED call with a0 as the code. */
_exit:
	la	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.


	.balign	4
trap_handler:
	csrr	t4, mcause
	la	t5, trap_mcause
	sd	t4, 0(t5)
	csrr	t4, mtval
	la	t5, trap_mtval
	sd	t4, 0(t5)
	csrr	t4, mepc

	/*
	 * Skip 2 insns (8 increment in mepc) to skip both the
	 * insn that caused the trap and the "j  _fail" insn
	 * that follows it.
	 */
	addi	t4, t4, 8
	csrw	mepc, t4
	mret

	.data
	.balign	8
semiargs:
	.space	16
trap_mcause:
	.space	8
trap_mtval:
	.space	8
