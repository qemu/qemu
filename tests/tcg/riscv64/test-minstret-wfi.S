/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc

	.text
	.global	_start
_start:
	lla	t0, trap
	csrw	mtvec, t0
	li	s3, 0

	/*
	 * A pending, locally enabled interrupt wakes WFI even with MIE
	 * clear.
	 */
	li	t0, 0x8		/* MIE_MSIE */
	csrw	mie, t0
	csrci	mstatus, 0x8	/* MSTATUS_MIE */
	li	t0, 0x2000000	/* MSIP0 */
	li	t1, 1
	sw	t1, 0(t0)

	/*
	 * The first CSR read and WFI retire before the second read obtains s1,
	 * so the expected difference is two.
	 */
	csrr	s0, minstret
	wfi
	csrr	s1, minstret
	sub	s1, s1, s0
	li	t1, 2
	beq	s1, t1, 1f
	ori	s3, s3, 1
1:

	/* Clear the software interrupt before testing the trap path. */
	sw	zero, 0(t0)
	csrw	mie, zero

	/* Schedule a timer interrupt far enough ahead to reach WFI first. */
	li	t0, 0x200bff8	/* MTIME */
	ld	t1, 0(t0)
	addi	t1, t1, 100
	li	t0, 0x2004000	/* MTIMECMP0 */
	sd	t1, 0(t0)
	li	t0, 0x80		/* MIE_MTIE */
	csrw	mie, t0
	csrsi	mstatus, 0x8	/* MSTATUS_MIE */

	/*
	 * The first CSR read and WFI retire before the trap handler obtains s1,
	 * so the expected difference is again two.
	 */
	li	s2, 1
	csrr	s0, minstret
	wfi
	beqz	s2, 1f
	ori	s3, s3, 2
1:
	bnez	s3, fail
	li	a0, 0
	j	_exit

fail:
	mv	a0, s3

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

	.balign	4
trap:
	/* Read minstret before retiring an instruction in the handler. */
	csrr	s1, minstret
	sub	s1, s1, s0
	addi	s1, s1, -2
	snez	s2, s1

	/* Disable the timer interrupt before returning past WFI. */
	li	t0, 0x2004000	/* MTIMECMP0 */
	li	t1, -1
	sd	t1, 0(t0)
	li	t0, 0x80		/* MIE_MTIE */
	csrc	mie, t0
	mret

	.data
	.balign	16
semiargs:
	.space	16
