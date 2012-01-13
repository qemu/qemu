# mach: crisv32
# output: ef\nef\n

; Check that "clearf x" doesn't trivially fail.

 .include "testutils.inc"
 start
 setf puixnzvc
 clearf x	; Actually, x would be cleared by almost-all other insns.
 move ccs,r3
 and.d 0xff, $r3
 checkr3 ef

 setf puixnzvc
 moveq 0, $r3	; moveq should only clear the xflag.
 move ccs,r3
 and.d 0xff, $r3
 checkr3 ef
 quit
