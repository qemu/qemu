/*
 * Test the SiFive Xsfvqmaccqoq/Xsfvqmaccdod custom int8 matrix-multiply
 * extensions.
 *
 * The eight instructions widen 8-bit integer inputs to 32 bits and perform
 * a matrix-multiply-accumulate into a vector of int32 accumulators:
 *
 *   C[j] += A * B[j]
 *
 * Xsfvqmaccqoq (4x8x4): A is 4x8, B[j] is 8x4, C[j] is 4x4 int32. One matrix
 * operation is performed per 32 elements of vl (TILE_SIZE = 32).
 * Xsfvqmaccdod (2x8x2): A is 2x8, B[j] is 8x2, C[j] is 2x2 int32. One matrix
 * operation is performed per 16 elements of vl (TILE_SIZE = 16).
 *
 * The input tiles below contain bytes >= 0x80 so that each signedness
 * variant produces a distinct result; the expected int32 tiles were computed
 * with an independent reference model of the specification. The accumulators
 * are cleared before every operation, so the expected value is the pure
 * matrix product.
 *
 * Test exits via semihosting with status 0 on success, or the 1-based index
 * of the first failing operation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option arch, +v
	.option norvc

	.text

/*
 * Run one 4x8x4 operation: load the 32-byte A and B tiles, clear the 4x4
 * int32 accumulator (vd = v4:v5, EMUL = 2), execute the op, then compare the
 * 16 int32 results against the expected tile.
 */
.macro RUN_QOQ id, word, exp
	li	a2, 32
	vsetvli	t0, a2, e8, m1, ta, ma
	la	t1, a_qoq
	vle8.v	v1, (t1)
	la	t1, b_qoq
	vle8.v	v2, (t1)
	vsetvli	t0, x0, e32, m2, ta, ma
	vmv.v.i	v4, 0
	li	a2, 32
	vsetvli	t0, a2, e8, m1, ta, ma
	.word	\word
	vsetvli	t0, x0, e32, m2, ta, ma
	la	t1, result
	vse32.v	v4, (t1)
	la	a0, result
	la	a1, \exp
	li	a2, 16
	li	t6, \id
	call	check_words
.endm

/*
 * Run one 2x8x2 operation: load the 16-byte A and B tiles, clear the 2x2
 * int32 accumulator (vd = v4, EMUL = 1), execute the op, then compare the
 * 4 int32 results against the expected tile.
 */
.macro RUN_DOD id, word, exp
	li	a2, 16
	vsetvli	t0, a2, e8, m1, ta, ma
	la	t1, a_dod
	vle8.v	v1, (t1)
	la	t1, b_dod
	vle8.v	v2, (t1)
	vsetvli	t0, x0, e32, m1, ta, ma
	vmv.v.i	v4, 0
	li	a2, 16
	vsetvli	t0, a2, e8, m1, ta, ma
	.word	\word
	vsetvli	t0, x0, e32, m1, ta, ma
	la	t1, result
	vse32.v	v4, (t1)
	la	a0, result
	la	a1, \exp
	li	a2, 4
	li	t6, \id
	call	check_words
.endm

	.global _start
_start:
	/* Enable the vector unit (mstatus.VS = Initial). */
	li	t0, 1 << 9
	csrs	mstatus, t0

	/* Route synchronous traps to trap_handler (mtvec direct mode). */
	la	t0, trap_handler
	csrw	mtvec, t0

	RUN_QOQ	1, 0xF220A25B, exp_qoq_u	/* sf.vqmaccu.4x8x4  */
	RUN_QOQ	2, 0xF620A25B, exp_qoq_s	/* sf.vqmacc.4x8x4   */
	RUN_QOQ	3, 0xFA20A25B, exp_qoq_us	/* sf.vqmaccus.4x8x4 */
	RUN_QOQ	4, 0xFE20A25B, exp_qoq_su	/* sf.vqmaccsu.4x8x4 */
	RUN_DOD	5, 0xB220A25B, exp_dod_u	/* sf.vqmaccu.2x8x2  */
	RUN_DOD	6, 0xB620A25B, exp_dod_s	/* sf.vqmacc.2x8x2   */
	RUN_DOD	7, 0xBA20A25B, exp_dod_us	/* sf.vqmaccus.2x8x2 */
	RUN_DOD	8, 0xBE20A25B, exp_dod_su	/* sf.vqmaccsu.2x8x2 */

	/*
	 * Illegal-instruction reporting test (op 9).
	 *
	 * When vl is not a multiple of the tile size the instruction is
	 * reserved and the helper raises an illegal-instruction exception at
	 * runtime.
	 */
	la	t1, trap_mtval
	sd	zero, 0(t1)
	la	t1, trap_mcause
	sd	zero, 0(t1)
	/* vl = 16 is not a multiple of the 4x8x4 tile size (32 elements). */
	li	a2, 16
	vsetvli	t0, a2, e8, m1, ta, ma
	.word	0xF220A25B		/* sf.vqmaccu.4x8x4, traps */
	/* The handler skips the faulting insn, so control resumes here. */
	li	a0, 9			/* fail code for this check */
	la	t1, trap_mcause
	ld	t2, 0(t1)
	li	t3, 2			/* RISCV_EXCP_ILLEGAL_INST */
	bne	t2, t3, _exit
	la	t1, trap_mtval
	ld	t2, 0(t1)
	li	t3, 0xF220A25B
	bne	t2, t3, _exit

	li	a0, 0
	j	_exit

/*
 * Machine-mode trap handler: record mcause and mtval, then advance mepc
 * past the 4-byte faulting instruction and return.
 */
	.balign	4
trap_handler:
	csrr	t4, mcause
	la	t5, trap_mcause
	sd	t4, 0(t5)
	csrr	t4, mtval
	la	t5, trap_mtval
	sd	t4, 0(t5)
	csrr	t4, mepc
	addi	t4, t4, 4
	csrw	mepc, t4
	mret

/*
 * check_words(a0 = result, a1 = expected, a2 = word count).
 * Returns to the caller if every word matches; otherwise exits with the
 * operation index held in t6.
 */
check_words:
1:	beqz	a2, 2f
	lw	t2, 0(a0)
	lw	t3, 0(a1)
	bne	t2, t3, 3f
	addi	a0, a0, 4
	addi	a1, a1, 4
	addi	a2, a2, -1
	j	1b
2:	ret
3:	mv	a0, t6
	j	_exit

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

	.data
	.balign	8
semiargs:
	.space	16
result:
	.space	64
trap_mcause:
	.space	8
trap_mtval:
	.space	8

/*
 * A (4x8, row-major) in bytes [0, 32) of vs1.
 */
a_qoq:
	.byte	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x80
	.byte	0xFF, 0x01, 0x00, 0x02, 0x7F, 0x03, 0x04, 0x05
	.byte	0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x08
	.byte	0x00, 0x00, 0x81, 0x00, 0x00, 0x02, 0x00, 0x03
/*
 * B (8x4, row-major) in bytes [0, 32) of vs2.
 */
b_qoq:
	.byte	0x01, 0x02, 0x03, 0x04
	.byte	0x05, 0x06, 0x07, 0x08
	.byte	0x80, 0x7F, 0xFF, 0x01
	.byte	0x00, 0x10, 0x20, 0x30
	.byte	0x02, 0x02, 0x02, 0x02
	.byte	0xFE, 0x01, 0x00, 0x7F
	.byte	0x03, 0x00, 0x81, 0x04
	.byte	0x40, 0x50, 0x60, 0x70
/*
 * A (2x8, row-major) in bytes [0, 16) of vs1.
 */
a_dod:
	.byte	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x80
	.byte	0xFF, 0x7F, 0x00, 0x02, 0x81, 0x03, 0x04, 0x05
/*
 * B (8x2, row-major) in bytes [0, 16) of vs2.
 */
b_dod:
	.byte	0x01, 0x02
	.byte	0x80, 0x7F
	.byte	0x03, 0xFF
	.byte	0x00, 0x10
	.byte	0x02, 0xFE
	.byte	0x7F, 0x01
	.byte	0x81, 0x04
	.byte	0x40, 0x50

	.balign	4
exp_qoq_u:
	.word	0x0000279e, 0x000029db, 0x0000371f, 0x00003bf7
	.word	0x00000648, 0x000004b5, 0x00000826, 0x0000091f
	.word	0x00007be0, 0x00002030, 0x000074f0, 0x000042f0
	.word	0x0000433c, 0x000040f1, 0x0000819f, 0x000002cf
exp_qoq_s:
	.word	0xffffde9e, 0xffffd9db, 0xffffcd1f, 0xffffcbf7
	.word	0x00000248, 0x000002b5, 0x00000126, 0x0000051f
	.word	0xffffebe0, 0x00002030, 0xffffd4f0, 0x000042f0
	.word	0x0000403c, 0xffffc1f1, 0x0000019f, 0x000001cf
exp_qoq_us:
	.word	0x00001e9e, 0x000029db, 0x00002d1f, 0x00003bf7
	.word	0x00000348, 0x000004b5, 0x00000426, 0x0000091f
	.word	0xffffebe0, 0x00002030, 0xffffd4f0, 0x000042f0
	.word	0xffffc03c, 0x000040f1, 0x0000009f, 0x000002cf
exp_qoq_su:
	.word	0xffffe79e, 0xffffd9db, 0xffffd71f, 0xffffcbf7
	.word	0x00000548, 0x000002b5, 0x00000526, 0x0000051f
	.word	0x00007be0, 0x00002030, 0x000074f0, 0x000042f0
	.word	0xffffc33c, 0xffffc1f1, 0xffff829f, 0x000001cf
exp_dod_u:
	.word	0x00002795, 0x00003155, 0x00004642, 0x0000c2c0
exp_dod_s:
	.word	0xffffde95, 0xffffd955, 0xffffc042, 0x000041c0
exp_dod_us:
	.word	0x00001e95, 0x00002955, 0xffffc342, 0x000041c0
exp_dod_su:
	.word	0xffffe795, 0xffffe155, 0x00004342, 0xffffc2c0
