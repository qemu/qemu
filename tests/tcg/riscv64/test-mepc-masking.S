/*
 * Test for MEPC masking bug fix
 *
 * This test verifies that MEPC properly masks the lower bits according
 * to the RISC-V specification when vectored mode bits from STVEC are
 * written to MEPC.
 */

	.option	norvc

	.text
	.global _start
_start:
	/* Set up machine trap vector */
	lla	t0, machine_trap_handler
	csrw	mtvec, t0

	/* Set STVEC with vectored mode (mode bits = 01) */
	li	t0, 0x80004001
	csrw	stvec, t0

	/* Clear medeleg to handle exceptions in M-mode */
	csrw	medeleg, zero

	/* Trigger illegal instruction exception */
	.word	0xffffffff

test_completed:
	/* Exit with result in a0 */
	/* a0 = 0: success (bits [1:0] were masked) */
	/* a0 != 0: failure (some bits were not masked) */
	j	_exit

machine_trap_handler:
	/* Check if illegal instruction (mcause = 2) */
	csrr	t0, mcause
	li	t1, 2
	bne	t0, t1, skip_test

	/* Test: Copy STVEC (with mode bits) to MEPC */
	csrr	t0, stvec	/* t0 = 0x80004001 */
	csrw	mepc, t0	/* Write to MEPC */
	csrr	t1, mepc	/* Read back MEPC */

	/* Check if bits [1:0] are masked (IALIGN=32 without RVC) */
	andi	a0, t1, 3	/* a0 = 0 if both bits masked correctly */

	/* Set correct return address */
	lla	t0, test_completed
	csrw	mepc, t0

skip_test:
	mret

/* Exit with semihosting */
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	8
semiargs:
	.space	16
