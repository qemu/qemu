# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 3\n3\nffffffff\nffffffff\n1f\nffffffe0\n7813445e\n

 .include "testutils.inc"
 start
 moveq 1,r3
 orq 2,r3
 test_move_cc 0 0 0 0
 checkr3 3

 moveq 2,r3
 orq 1,r3
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0xf0ff,r3
 orq -1,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 0,r3
 orq -1,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 0,r3
 orq 31,r3
 test_move_cc 0 0 0 0
 checkr3 1f

 moveq 0,r3
 orq -32,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffe0

 move.d 0x78134452,r3
 orq 12,r3
 test_move_cc 0 0 0 0
 checkr3 7813445e

 quit
