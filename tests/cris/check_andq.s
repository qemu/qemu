# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\nffff\nffffffff\n1f\nffffffe0\n78134452\n0\n

 .include "testutils.inc"
 start
 moveq -1,r3
 andq 2,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 andq -1,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 andq -1,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r3
 andq -1,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 andq 31,r3
 test_move_cc 0 0 0 0
 checkr3 1f

 moveq -1,r3
 andq -32,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffe0

 move.d 0x78134457,r3
 andq -14,r3
 test_move_cc 0 0 0 0
 checkr3 78134452

 moveq 0,r3
 andq -14,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
