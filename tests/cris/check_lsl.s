# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\n4\n80000000\nffff8000\n7f19f000\n80000000\n0\n0\n699fc67c\nffffffff\n4\n80000000\nffff8000\n7f19f000\nda670000\nda670000\nda670000\nda67c67c\nffffffff\nfffafffe\n4\nffff0000\nffff8000\n5a67f000\nda67f100\nda67f100\nda67f100\nda67f17c\nfff3faff\nfff3fafe\n4\nffffff00\nffffff00\nffffff80\n5a67f100\n5a67f1f0\n

 .include "testutils.inc"
 start
 moveq -1,r3
 lslq 0,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 lslq 1,r3
 test_move_cc 0 0 0 0
 checkr3 4

 moveq -1,r3
 lslq 31,r3
 test_move_cc 1 0 0 0
 checkr3 80000000

 moveq -1,r3
 lslq 15,r3
 test_move_cc 1 0 0 0
 checkr3 ffff8000

 move.d 0x5a67f19f,r3
 lslq 12,r3
 test_move_cc 0 0 0 0
 checkr3 7f19f000

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsl.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 80000000

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsl.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsl.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsl.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 699fc67c

 moveq -1,r3
 moveq 0,r4
 lsl.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 moveq 1,r4
 lsl.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 4

 moveq -1,r3
 moveq 31,r4
 lsl.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 80000000

 moveq -1,r3
 moveq 15,r4
 lsl.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffff8000

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsl.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 7f19f000

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsl.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsl.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsl.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsl.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67c67c

 moveq -1,r3
 moveq 0,r4
 lsl.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0xfffaffff,r3
 moveq 1,r4
 lsl.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 fffafffe

 moveq 2,r3
 moveq 1,r4
 lsl.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 4

 moveq -1,r3
 moveq 31,r4
 lsl.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffff0000

 moveq -1,r3
 moveq 15,r4
 lsl.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffff8000

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsl.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 5a67f000

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsl.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 da67f17c

 move.d 0xfff3faff,r3
 moveq 0,r4
 lsl.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 fff3faff

 move.d 0xfff3faff,r3
 moveq 1,r4
 lsl.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 fff3fafe

 moveq 2,r3
 moveq 1,r4
 lsl.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 4

 moveq -1,r3
 moveq 31,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffffff00

 moveq -1,r3
 moveq 15,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffffff00

 moveq -1,r3
 moveq 7,r4
 lsl.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffff80

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsl.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 5a67f100

 move.d 0x5a67f19f,r3
 moveq 4,r4
 lsl.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 5a67f1f0

 quit
