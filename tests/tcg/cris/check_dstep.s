# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: fffffffc\n4\nffff\nfffffffe\n9bf3911b\n0\n

 .include "testutils.inc"
 start
 moveq -1,r3
 moveq 2,r4
 dstep r4,r3
 test_move_cc 1 0 0 0
 checkr3 fffffffc

 moveq 2,r3
 moveq -1,r4
 dstep r4,r3
 test_move_cc 0 0 0 0
 checkr3 4

 move.d 0xffff,r4
 move.d r4,r3
 dstep r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r4
 move.d r4,r3
 dstep r4,r3
 test_move_cc 1 0 0 0
 checkr3 fffffffe

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 dstep r4,r3
 test_move_cc 1 0 0 0
 checkr3 9bf3911b

 move.d 0xffff,r3
 move.d 0x1fffe,r4
 dstep r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
