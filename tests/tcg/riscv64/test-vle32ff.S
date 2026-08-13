/*
 * Verify that a fault-only-first load keeps vl unchanged when element zero
 * raises a synchronous exception.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option norvc

	.text
	.globl _start
_start:
	lla	t0, trap
	csrw	mtvec, t0

	li	t0, (1 << 9)	/* Set mstatus.VS to Initial. */
	csrs	mstatus, t0
	li	t1, 4
	vsetvli	t2, t1, e32, m1, ta, ma
	/* Unassigned MMIO: probe succeeds, load faults. */
	li	t0, 0x18000000
	vle32ff.v	v1, (t0)

	/* Element zero did not trap. */
	li	a0, 1
	j	_exit

trap:
	csrr	t0, mcause
	li	t1, 5		/* Load access fault. */
	bne	t0, t1, trap_fail

	csrr	t0, vl
	bne	t0, t2, trap_fail

	li	a0, 0
	j	_exit

trap_fail:
	li	a0, 1

_exit:
	lla	a1, semiargs
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
