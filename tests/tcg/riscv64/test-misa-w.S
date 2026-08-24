/*
 * Test for MISA changing C and related IALIGN alignment cases
 *
 * This test verifies that the "C" extension can be cleared and set in MISA,
 * that a branch to 2-byte aligned instructions can be executed when "C" is
 * enabled, and that a write to MISA which would increase IALIGN and cause
 * the next instruction to be unaligned is ignored.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define RVC (1 << ('C'-'A'))
#define RVV (1 << ('V'-'A'))

.option norvc
	.text
	.global _start
_start:
	lla	t0, trap
	csrw	mtvec, t0

	csrr	t0, misa
	li	t1, RVC
	not	t1, t1
	and	t0, t0, t1
	csrw	misa, t0
	csrr	t1, misa
	li	a0, 2 # fail code
	bne	t0, t1, _exit # Could not clear RVC in MISA

	li	t1, RVC
	or	t0, t0, t1
	csrw	misa, t0
	csrr	t1, misa
	li	a0, 3 # fail code
	bne	t0, t1, _exit # Could not set RVC in MISA

	j	unalign
. = . + 2
unalign:

	li	t1, RVC
	not	t1, t1
	and	t0, t0, t1
	csrw	misa, t0
	csrr	t1, misa
	li	a0, 4 # fail code
	beq	t0, t1, _exit # Was able to clear RVC in MISA

	li	t0, (RVC|RVV)
	not	t0, t0
	and	t0, t0, t1
	csrw	misa, t0
	csrr	t0, misa
	li	a0, 5 # fail code
	bne	t0, t1, _exit # MISA write was not ignored (RVV was cleared)

	j	realign
. = . + 2
realign:

	# Success!
	li	a0, 0
	j	_exit

trap:
	# Any trap is a fail code 1
	li	a0, 1

# Exit code in a0
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	# ADP_Stopped_ApplicationExit
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	# TARGET_SYS_EXIT_EXTENDED

	# Semihosting call sequence
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
