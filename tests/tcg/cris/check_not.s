# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: fffffffe\nfffffffd\nffff0f00\n0\n87ecbbad\n

 .include "testutils.inc"
 start
 moveq 1,r3
 not r3
 test_move_cc 1 0 0 0
 checkr3 fffffffe

 moveq 2,r3
 not r3
 test_move_cc 1 0 0 0
 checkr3 fffffffd

 move.d 0xf0ff,r3
 not r3
 test_move_cc 1 0 0 0
 checkr3 ffff0f00

 moveq -1,r3
 not r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0x78134452,r3
 not r3
 test_move_cc 1 0 0 0
 checkr3 87ecbbad

 quit
