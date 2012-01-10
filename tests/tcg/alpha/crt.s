	.text

	.globl _start
	.ent _start,0
_start:
	.frame $15,0,$15
	br $29,1f
1:	ldgp $29, 0($29)
	.prologue 0
	ldq $27,main($29) !literal!1
	jsr $26,($27)
	or $0,$0,$16
	.end _start

	.globl _exit
_exit:
	lda $0,1
	callsys

	call_pal 0

	.globl write
write:
	lda $0,4
	callsys
	ret
