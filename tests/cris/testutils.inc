	.syntax	no_register_prefix

	.macro	start
	.text
	.global	main
main:
	.endm

	.macro	quit
	jump	pass
	nop
	.endm

	.macro	pass
	jump	pass
	nop
	.endm

	.macro	startnostack
	start
	.endm

	.macro	fail
	.data
99:
	.asciz " checkr3 failed\n"
	.text
	move.d	99b, $r10
	jsr	_fail
	nop
	.endm

	.macro	checkr3 val
	cmp.d	0x\val, $r3
	beq	100f
	nop
	.data
99:
	.asciz "checkr3 failed\n"
	.text
	move.d	99b, $r10
	jsr	_fail
	nop
100:
	.endm

; Test the condition codes
        .macro test_cc N Z V C
        .if \N
        bpl 9f
        nop
        .else
        bmi 9f
        nop
        .endif
        .if \Z
        bne 9f
        nop
        .else
        beq 9f
        nop
        .endif
        .if \V
        bvc 9f
        nop
        .else
        bvs 9f
        nop
        .endif
        .if \C
        bcc 9f
        nop
        .else
        bcs 9f
        nop
        .endif
        ba 8f
        nop
9:
	.data
99:
	.asciz "test_move_cc failed\n"
	.text
	move.d	99b, $r10
	jsr	_fail
	nop
8:
        .endm


        .macro test_move_cc N Z V C
        .if \N
        bpl 9f
        nop
        .else
        bmi 9f
        nop
        .endif
        .if \Z
        bne 9f
        nop
        .else
        beq 9f
        nop
        .endif
        ba 8f
        nop
9:
	.data
99:
	.asciz "test_move_cc failed\n"
	.text
	move.d	99b, $r10
	jsr	_fail
	nop
8:
        .endm
