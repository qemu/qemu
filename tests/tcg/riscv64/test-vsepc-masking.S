/* SPDX-License-Identifier: GPL-2.0-or-later */

#define MISA_C          (1 << 2)

	.option	norvc
	.text
	.global	_start
_start:
	csrr	s0, misa
	andi	s0, s0, MISA_C

	lla	s1, test_addr
	ori	t0, s1, 3
	csrw	vsepc, t0
	csrr	t1, vsepc

	beqz	s0, ialign32
	addi	t2, s1, 2	/* IALIGN=16: bit 0 reads as zero. */
	j	check

ialign32:
	/* IALIGN=32: bits [1:0] read as zero. */
	mv	t2, s1

check:
	li	a0, 2
	bne	t1, t2, _exit

	li	a0, 0
	j	_exit

	.balign	4
test_addr:
	nop

/* Exit code in a0. */
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence. */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
