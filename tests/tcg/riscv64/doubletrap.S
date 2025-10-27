	.option norvc

	.text
	.global _start
_start:
	# Set up vectored interrupts
	lla	t0, trap
	add	t0, t0, 1
	csrw	mtvec, t0

	# Enable sw interrupts
	csrrsi	zero, mie, 0x8
	csrrsi	zero, mstatus, 0x8

	# Engage the double trap: we trigger an machine-level software
	# interrupt, which will trap to an illegal instruction
	lui	t1, 0x02000
	li	t0, 1
	sw	t0, 0(t1)

	# If we still not went out via the software interrupt route after a
	# short while, we failed the test.
	lui	t0, 0x1
0:
	addi	t0, t0, -1
	bnez	t0, 0b
	j	fail

trap:
	j	illegal_insn # Exceptions
	j	fail # Supervisor software interrupt
	j	fail
	.insn	i CUSTOM_0, 0, x0, x0, 0 # Machine software interrupt
	j	fail
	j	fail # Supervisor timer interrupt
	j	fail
	j	fail # Machine timer interrupt
	j	fail
	j	fail # Supervisor external interrupt
	j	fail
	j	fail # Machine external interrupt
	j	fail
	j	fail # Counter overflow interrupt
	j	fail
	j	fail

illegal_insn:
	# Check whether we really got an illegal instruction
	csrr	t0, mcause
	li	t1, 2
	bne	t0, t1, fail
	li	a0, 0
	j	_exit
fail:
	li	a0, 1
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
