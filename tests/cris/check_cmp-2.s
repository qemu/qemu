

.include "testutils.inc"

	start

	move.d	4294967283, $r0
	move.d	$r0, $r10
	cmp.d	$r0, $r10
	beq	1f
	move.d $r10, $r3
	fail
1:
	pass
	quit