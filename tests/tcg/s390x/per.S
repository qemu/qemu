	.org	0x8d
ilc:
	.org	0x8e
program_interruption_code:
	.org	0x96
per_code:
	.org	0x98
per_address:
	.org	0x150
program_old_psw:
	.org	0x1d0
program_new_psw:
	.quad	0, pgm_handler

	.org	0x200			/* exit lowcore */

per_on_psw:
	.quad	0x4000000000000000, start_per
per_on_regs:
	.quad	0x80000000, 0, -1	/* successful-branching everywhere */
per_off_regs:
	.quad	0, 0 ,0
success_psw:
	.quad	0x2000000000000, 0xfff	/* see is_special_wait_psw() */
failure_psw:
	.quad	0x2000000000000, 0	/* disabled wait */

	.org	0x2000			/* exit lowcore pages */

	.globl _start
_start:
	lpswe	per_on_psw
start_per:
	lctlg	%c9, %c11, per_on_regs

/* Test unconditional relative branch. */
	larl	%r0, j1
	larl	%r1, d1
	lhi	%r2, 0
j1:	j	d1
	lpswe	failure_psw
d1:

/* Test unconditional indirect branch. */
	larl	%r0, j2
	larl	%r1, d2
j2:	br	%r1
	lpswe	failure_psw
d2:

/* Test conditional relative branch. */
	larl	%r0, j3
	larl	%r1, d3
	clr	%r1, %r2	/* d3 != 0 */
j3:	jne	d3
	lpswe	failure_psw
d3:

/* Test conditional register branch. */
	larl	%r0, j4
	larl	%r1, d4
	clr	%r1, %r2	/* d4 != 0 */
j4:	bner	%r1
	lpswe	failure_psw
d4:

/* Success! */
	nop
	lpswe	success_psw

pgm_handler:
	chhsi	program_interruption_code, 0x80	/* PER event? */
	jne	fail
	cli	per_code, 0x80		/* successful-branching event? */
	jne	fail
	clg	%r0, per_address	/* per_address == jump insn? */
	jne	fail
	clg	%r1, program_old_psw+8	/* psw.addr updated to dest? */
	jne	fail
	lpswe	program_old_psw
fail:
	lpswe	failure_psw
