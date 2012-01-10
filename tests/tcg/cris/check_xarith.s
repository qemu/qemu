
.include "testutils.inc"

	start

	moveq	-1, $r0
	moveq	0, $r1
	addq	1, $r0
	ax
	addq	0, $r1

	move.d	$r0, $r3
	checkr3 0
	move.d	$r1, $r3
	checkr3 1

	move.d  0, $r0
	moveq	-1, $r1
	subq	1, $r0
	ax
	subq	0, $r1

	move.d	$r0, $r3
	checkr3 ffffffff
	move.d	$r1, $r3
	checkr3 fffffffe


	moveq	-1, $r0
	moveq	-1, $r1
	cmpq	-1, $r0
	ax
	cmpq	-1, $r1
	beq	1f
	nop
	fail
1:
	cmpq	0, $r0
	ax
	cmpq	-1, $r1
	bne	1f
	nop
	fail
1:

	;; test for broken X sequence, run it several times.
	moveq	8, $r0
1:
	moveq	0, $r3
	move.d	$r0, $r1
	andq	1, $r1
	lslq	4, $r1
	moveq	1, $r2
	or.d	$r1, $r2
	ba	2f
	move	$r2, $ccs
2:
	addq	0, $r3
	move.d	$r0, $r4
	move.d	$r1, $r5
	move.d	$r2, $r6
	move.d	$r3, $r7
	lsrq	4, $r1
	move.d	$r1, $r8
	xor	$r1, $r3
	checkr3	0
	subq	1, $r0
	bne	1b
	nop

	pass
	quit
