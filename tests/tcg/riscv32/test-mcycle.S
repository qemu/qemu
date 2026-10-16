/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc

	.text
	.global	_start
_start:
	/* Exercise writes while mcycle is running. */
	csrw	mcountinhibit, zero
	csrw	mcycle, zero

	/* A write to the high half must be immediately observable. */
	li	s0, 0x1234ffff
	csrw	mcycleh, s0
	csrr	t0, mcycleh
	bne	t0, s0, fail

	/* Check carry from the low half into the high half. */
	li	s0, 0x12345678
	csrw	mcycleh, s0
	li	t0, 0xfffffff0
	csrw	mcycle, t0
	.rept	32
	nop
	.endr
	csrr	t0, mcycleh
	addi	s0, s0, 1
	bne	t0, s0, fail

	/* A low-half write must preserve the carried high half. */
	li	t0, 0x22222222
	csrw	mcycle, t0
	csrr	t0, mcycleh
	bne	t0, s0, fail

	li	t0, 0x100000
	li	t1, 0x5555	/* FINISHER_PASS */
	sw	t1, 0(t0)
	j	.

fail:
	li	t0, 0x100000
	li	t1, 0x13333	/* status = FINISHER_FAIL, code = 1 */
	sw	t1, 0(t0)
	j	.
