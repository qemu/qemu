	.data
_stack_start:
	.space	8192, 0
_stack_end:
	.text
	.global	_start
_start:
	move.d	_stack_end, $sp
	jsr	main
	nop
	moveq	0, $r10
	jump	exit
	nop
