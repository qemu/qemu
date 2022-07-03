	.option	norvc

	.text
	.global _start
_start:
	lla	t0, trap
	csrw	mtvec, t0

	# These are all illegal instructions
	csrw	time, x0
	.insn	i CUSTOM_0, 0, x0, x0, 0x321
	csrw	time, x0
	.insn	i CUSTOM_0, 0, x0, x0, 0x123
	csrw	cycle, x0

	# Success!
	li	a0, 0
	j	_exit

trap:
	# When an instruction traps, compare it to the insn in memory.
	csrr	t0, mepc
	csrr	t1, mtval
	lwu	t2, 0(t0)
	bne	t1, t2, fail

	# Skip the insn and continue.
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

fail:
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
