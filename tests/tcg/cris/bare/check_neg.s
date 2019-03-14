# mach:  crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\nffffffff\n0\n80000000\n1\nba987655\nffff\nffff\n0\n89ab8000\nffff0001\n45677655\nff\nff\n0\n89abae80\nffffff01\n45678955\n

 .include "testutils.inc"
 start
 moveq 0,r3
 moveq 1,r4
 neg.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 1,r3
 moveq 0,r4
 neg.d r3,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

;; FIXME: this was wrong.
 moveq 0,r3
 neg.d r3,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0x80000000,r3
 neg.d r3,r3
 test_move_cc 1 0 0 0
 checkr3 80000000

 moveq -1,r3
 neg.d r3,r3
 test_move_cc 0 0 0 0
 checkr3 1

 move.d 0x456789ab,r3
 neg.d r3,r3
 test_move_cc 1 0 0 0
 checkr3 ba987655

 moveq 0,r3
 moveq 1,r4
 neg.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffff

 moveq 1,r3
 moveq 0,r4
 neg.w r3,r3
 test_move_cc 1 0 0 0
 checkr3 ffff

 moveq 0,r3
 neg.w r3,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0x89ab8000,r3
 neg.w r3,r3
 test_move_cc 1 0 0 0
 checkr3 89ab8000

 moveq -1,r3
 neg.w r3,r3
 test_move_cc 0 0 0 0
 checkr3 ffff0001

 move.d 0x456789ab,r3
 neg.w r3,r3
 test_move_cc 0 0 0 0
 checkr3 45677655

 moveq 0,r3
 moveq 1,r4
 neg.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ff

 moveq 1,r3
 moveq 0,r4
 neg.b r3,r3
 test_move_cc 1 0 0 0
 checkr3 ff

 moveq 0,r3
 neg.b r3,r3
 test_move_cc 0 1 0 0
 checkr3 0

;; FIXME: was wrong.
 move.d 0x89abae80,r3
 neg.b r3,r3
 test_move_cc 1 0 0 1
 checkr3 89abae80

 moveq -1,r3
 neg.b r3,r3
 test_move_cc 0 0 0 0
 checkr3 ffffff01

 move.d 0x456789ab,r3
 neg.b r3,r3
 test_move_cc 0 0 0 0
 checkr3 45678955

 quit
