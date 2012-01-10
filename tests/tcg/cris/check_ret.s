# mach: crisv3 crisv8 crisv10
# output: 3\n

# Test that ret works.

 .include "testutils.inc"
 start
x:
 moveq 0,r3
 jsr z
w:
 quit
y:
 addq 1,r3
 checkr3 3
 quit

z:
 addq 1,r3
 move srp,r2
 add.d y-w,r2
 move r2,srp
 ret
 addq 1,r3
 quit
