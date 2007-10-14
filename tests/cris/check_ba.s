# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: a\n


 .set smalloffset,0
 .set largeoffset,0


	.macro fail
	jump _fail
	.endm

	.global	main
main:
 moveq 0,$r3

; Short forward branch.
 ba 0f
 addq 1,$r3
 fail

; Max short forward branch.
1:
 ba 2f
 addq 1,$r3
 fail

; Short backward branch.
0:
 ba 1b
 addq 1,$r3
 fail

 .space 254-2+smalloffset+1b-.,0
 moveq 0,$r3

2:
; Transit branch (long).
 ba 3f
 addq 1,$r3
 fail

 moveq 0,$r3
4:
; Long forward branch.
 ba 5f
 addq 1,$r3
 fail

 .space 256-2-smalloffset+4b-.,0

 moveq 0,$r3

; Max short backward branch.
3:
 ba 4b
 addq 1,$r3
 fail

5:
; Max long forward branch.
 ba 6f
 addq 1,$r3
 fail

 .space 32766+largeoffset-2+5b-.,0

 moveq 0,$r3
6:
; Transit branch.
 ba 7f
 addq 1,$r3
 fail

 moveq 0,$r3
9:
 jsr pass
 nop

; Transit branch.
 moveq 0,$r3
7:
 ba 8f
 addq 1,$r3
 fail

 .space 32768-largeoffset+9b-.,0

8:
; Max long backward branch.
 ba 9b
 addq 1,$r3
 fail
