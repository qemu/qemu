
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
	pass
	quit
