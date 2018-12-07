# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 3\n3\nffff\nffffffff\n7c33f7db\nffff0003\n3\nfedaffff\n7813f7db\n3\n3\nfeb\n781344db\n

 .include "testutils.inc"
 start
 moveq 1,r3
 moveq 2,r4
 or.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 3

 moveq 2,r3
 moveq 1,r4
 or.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0xff0f,r4
 move.d 0xf0ff,r3
 or.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r4
 move.d r4,r3
 or.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 or.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 7c33f7db

 move.d 0xffff0001,r3
 moveq 2,r4
 or.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff0003

 moveq 2,r3
 move.d 0xffff0001,r4
 or.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0xfedaffaf,r3
 move.d 0xffffff5f,r4
 or.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 fedaffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 or.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 7813f7db

 moveq 1,r3
 move.d 0xffffff02,r4
 or.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 3

 moveq 2,r3
 moveq 1,r4
 or.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0x4a,r4
 move.d 0xfa3,r3
 or.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 feb

 move.d 0x5432f789,r4
 move.d 0x78134453,r3
 or.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 781344db

 quit
