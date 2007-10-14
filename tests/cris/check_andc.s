# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\nffff\nffffffff\n50124400\nffff0002\n2\nfffff\nfedaff0f\n78134400\nffffff02\n2\nf02\n78134401\n78134400\n

 .include "testutils.inc"
 start
 moveq -1,r3
 and.d 2,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 and.d -1,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 and.d 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r3
 and.d -1,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 and.d 0x5432f789,r3
 test_move_cc 0 0 0 0
 checkr3 50124400

 moveq -1,r3
 and.w 2,r3
 test_move_cc 0 0 0 0
 checkr3 ffff0002

 moveq 2,r3
 and.w -1,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xfffff,r3
 and.w 0xffff,r3
 test_move_cc 1 0 0 0
 checkr3 fffff

 move.d 0xfedaffaf,r3
 and.w 0xff5f,r3
 test_move_cc 1 0 0 0
 checkr3 fedaff0f

 move.d 0x78134452,r3
 and.w 0xf789,r3
 test_move_cc 0 0 0 0
 checkr3 78134400

 moveq -1,r3
 and.b 2,r3
 test_move_cc 0 0 0 0
 checkr3 ffffff02

 moveq 2,r3
 and.b -1,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xfa7,r3
 and.b 0x5a,r3
 test_move_cc 0 0 0 0
 checkr3 f02

 move.d 0x78134453,r3
 and.b 0x89,r3
 test_move_cc 0 0 0 0
 checkr3 78134401

 and.b 0,r3
 test_move_cc 0 1 0 0
 checkr3 78134400

 quit
