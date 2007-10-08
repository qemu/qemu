# mach: crisv3 crisv8 crisv10 crisv32
# output: ffffff05\nffff0005\n5\nffffff00\n

; Move between registers.  Check that just the subreg is copied.

 .include "testutils.inc"
 startnostack
 moveq -30,r3
 moveq 5,r4
 move.b r4,r3
 test_move_cc 0 0 0 0  		; FIXME
 checkr3 ffffff05

 move.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff0005

 move.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 5

 moveq -1,r3
 moveq 0,r4
 move.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffffff00

 quit

