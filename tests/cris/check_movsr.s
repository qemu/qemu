# mach: crisv3 crisv8 crisv10 crisv32
# output: 5\nfffffff5\n5\nfffffff5\n0\n

; Movs between registers.  Check that sign-extension is performed and the
; full register is set.

 .include "testutils.inc"
 start
 moveq -1,r5
 moveq 5,r4
 move.b r4,r5
 moveq -1,r3
 movs.b r5,r3
 test_move_cc 0 0 0 0
 checkr3 5

 moveq 0,r5
 moveq -11,r4
 move.b r4,r5
 moveq 0,r3
 movs.b r5,r3
 test_move_cc 1 0 0 0
 checkr3 fffffff5

 moveq -1,r5
 moveq 5,r4
 move.w r4,r5
 moveq -1,r3
 movs.w r5,r3
 test_move_cc 0 0 0 0
 checkr3 5

 moveq 0,r5
 moveq -11,r4
 move.w r4,r5
 moveq 0,r3
 movs.w r5,r3
 test_move_cc 1 0 0 0
 checkr3 fffffff5

 moveq 0,r5
 movs.b r5,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
