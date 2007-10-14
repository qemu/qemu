# mach: crisv32
# output: 15\n7\n2\nffff1234\nb\n16\nf\n2\nffffffef\nf\nffff1234\nf\nfffffff4\nd\nfffffff2\n10\nfffffff2\nd\n

 .include "testutils.inc"
 .data
x:
 .dword 8,9,10,11
y:
 .dword -12,13,-14,15,16

 start
 moveq 7,r0
 moveq 2,r1
 move.d 0xffff1234,r2
 moveq 21,r3
 move.d x,r4
 setf zcvn
 movem r2,[r4+]
 test_cc 1 1 1 1
 subq 12,r4

 checkr3 15

 move.d [r4+],r3
 checkr3 7

 move.d [r4+],r3
 checkr3 2

 move.d [r4+],r3
 checkr3 ffff1234

 move.d [r4+],r3
 checkr3 b

 subq 16,r4
 moveq 22,r0
 moveq 15,r1
 clearf zcvn
 movem r0,[r4]
 test_cc 0 0 0 0
 move.d [r4+],r3
 checkr3 16

 move.d r1,r3
 checkr3 f

 move.d [r4+],r3
 checkr3 2

 subq 8,r4
 moveq 10,r2
 moveq -17,r0
 clearf zc
 setf vn
 movem r1,[r4]
 test_cc 1 0 1 0
 move.d [r4+],r3
 checkr3 ffffffef

 move.d [r4+],r3
 checkr3 f

 move.d [r4+],r3
 checkr3 ffff1234

 move.d y,r4
 setf zc
 clearf vn
 movem [r4+],r3
 test_cc 0 1 0 1
 checkr3 f

 move.d r0,r3
 checkr3 fffffff4

 move.d r1,r3
 checkr3 d

 move.d r2,r3
 checkr3 fffffff2

 move.d [r4],r3
 checkr3 10

 subq 8,r4
 setf zcvn
 movem [r4+],r0
 test_cc 1 1 1 1
 move.d r0,r3
 checkr3 fffffff2

 move.d r1,r3
 checkr3 d

 quit

