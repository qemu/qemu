# mach: crisv32
# output: fffffffe\n
# output: ffffffff\n
# output: fffffffe\n
# output: 1\n
# output: fffffffe\n
# output: ffffffff\n
# output: fffffffe\n
# output: 1\n

; Check that carry is not modified on v32.

 .include "testutils.inc"
 start
 moveq -1,r3
 moveq 2,r4
 setf c
 muls.d r4,r3
 test_cc 1 0 0 1
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq -1,r3
 moveq 2,r4
 setf c
 mulu.d r4,r3
 test_cc 0 0 1 1
 checkr3 fffffffe
 move mof,r3
 checkr3 1

 moveq -1,r3
 moveq 2,r4
 clearf c
 muls.d r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq -1,r3
 moveq 2,r4
 clearf c
 mulu.d r4,r3
 test_cc 0 0 1 0
 checkr3 fffffffe
 move mof,r3
 checkr3 1

 quit
