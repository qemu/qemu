# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\n1\n1\n1ffff\n5a67f\n1\n0\n0\n3699fc67\nffffffff\n1\n1\n1ffff\n5a67f\nda670000\nda670000\nda670000\nda673c67\nffffffff\nffff7fff\n1\nffff0000\nffff0001\n5a67000f\nda67f100\nda67f100\nda67f100\nda67f127\nffffffff\nffffff7f\n1\nffffff00\nffffff00\nffffff01\n5a67f100\n5a67f109\n

 .include "testutils.inc"
 start
 moveq -1,r3
 lsrq 0,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 lsrq 1,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 lsrq 31,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 lsrq 15,r3
 test_move_cc 0 0 0 0
 checkr3 1ffff

 move.d 0x5a67f19f,r3
 lsrq 12,r3
 test_move_cc 0 0 0 0
 checkr3 5a67f

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsr.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsr.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 3699fc67

 moveq -1,r3
 moveq 0,r4
 lsr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 moveq 1,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 31,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 15,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 1ffff

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 5a67f

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsr.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsr.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsr.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 da670000

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 da673c67

 moveq -1,r3
 moveq 0,r4
 lsr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 1,r4
 lsr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff7fff

 moveq 2,r3
 moveq 1,r4
 lsr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

;; FIXME: this was wrong. Z should be set.
 moveq -1,r3
 moveq 31,r4
 lsr.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffff0000

 moveq -1,r3
 moveq 15,r4
 lsr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff0001

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 5a67000f

 move.d 0xda67f19f,r3
 move.d 31,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 32,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 33,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 da67f100

 move.d 0xda67f19f,r3
 move.d 66,r4
 lsr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 da67f127

 moveq -1,r3
 moveq 0,r4
 lsr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 1,r4
 lsr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffffff7f

 moveq 2,r3
 moveq 1,r4
 lsr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 31,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffffff00

 moveq -1,r3
 moveq 15,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 ffffff00

 moveq -1,r3
 moveq 7,r4
 lsr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffffff01

 move.d 0x5a67f19f,r3
 moveq 12,r4
 lsr.b r4,r3
 test_move_cc 0 1 0 0
 checkr3 5a67f100

 move.d 0x5a67f19f,r3
 moveq 4,r4
 lsr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 5a67f109

 quit
