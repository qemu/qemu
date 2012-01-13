# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\nffff\nffffffff\n5432f789\n2\n2\nffff\nffff\nffff\nf789\n2\n2\nff\nff\n89\nfeda4953\nfeda4962\n0\n0\n

 .include "testutils.inc"
 start
 moveq -1,r3
 moveq 2,r4
 bound.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 moveq -1,r4
 bound.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r4
 move.d r4,r3
 bound.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r4
 move.d r4,r3
 bound.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 bound.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 5432f789

 moveq -1,r3
 moveq 2,r4
 bound.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 moveq -1,r4
 bound.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq -1,r3
 bound.w r3,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 move.d 0xffff,r4
 move.d r4,r3
 bound.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 move.d 0xfedaffff,r4
 move.d r4,r3
 bound.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 bound.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 f789

 moveq -1,r3
 moveq 2,r4
 bound.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 moveq -1,r4
 bound.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xff,r4
 move.d r4,r3
 bound.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 move.d 0xfeda49ff,r4
 move.d r4,r3
 bound.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 bound.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 89

 move.d 0xfeda4956,r3
 move.d 0xfeda4953,r4
 bound.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 feda4953

 move.d 0xfeda4962,r3
 move.d 0xfeda4963,r4
 bound.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 feda4962

 move.d 0xfeda4956,r3
 move.d 0,r4
 bound.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xfeda4956,r4
 move.d 0,r3
 bound.d r4,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
