	.option norvc

	.text
	.global _start
_start:
	# Set up trap vector
	lla	t0, trap
	csrw	mtvec, t0

	# Set up timer
	lui	t1, 0x02004
	sd	zero, 0(t1) # MTIMECMP0

	# Enable timer interrupts
	li	t0, 0x80
	csrrs	zero, mie, t0
	csrrsi	zero, mstatus, 0x8

	# Set up UART
	lui	t1, 0x10000
	li	a0, 0x80	# DLAB=1
	sb	a0, 3(t1)
	li	a0, 1		# Full speed
	sw	a0, 0(t1)
	li	a0, 0x03	# 8N1, DLAB=0
	sb	a0, 3(t1)

	# Run test for around 60s
	call	rtc_get
	li	t0, 30
	slli	t0, t0, 30 # Approx. 10e9 ns
	add	t0, t0, a0
0:
	# Tight loop with memory accesses
	li	a1, 1000000
	la	a2, semiargs
1:
	ld	a0, 0(a2)
	sd	a0, 0(a2)
	addi	a1, a1, -1
	bnez	a1, 1b

	li	a0, '.'
	call	send_byte
	call	rtc_get
	bltu	a0, t0, 0b

	li	a0, '\n'
	call	send_byte

	# Exit
	li	a0, 0
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

rtc_get:
	# Get current time from the goldfish RTC
	lui	t3, 0x0101
	lw	a0, 0(t3)
	lw	t3, 4(t3)
	slli	t3, t3, 32
	add	a0, a0, t3
	ret

send_byte:
	# Send a single byte over the serial
	lui	t3, 0x10000
	lb	a1, 5(t3)
	andi	a1, a1, 0x20
	beqz	a1, send_byte
	sb	a0, 0(t3)
	ret

	.balign 4
trap:
	lui	t5, 0x0200c
	ld	t6, -0x8(t5) # MTIME
	addi	t6, t6, 100
	lui	t5, 0x02004
	sd	t6, 0(t5) # MTIMECMP
	mret

	.data
	.balign	16
semiargs:
	.space	16
