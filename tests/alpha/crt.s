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

	lda $0,1
	callsys

	call_pal 0
	.end _start

	.globl write
write:
	lda $0,4
	callsys
	ret
