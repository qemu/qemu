# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\nffff\nffffffff\n5432f789\n2\nffff\n2\nffff\nffff\nf789\n2\n2\nff\nff\nff\n89\n0\nff\n

 .include "testutils.inc"
 start
 moveq -1,r3
 moveq 2,r4
 bound.d 2,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 bound.d 0xffffffff,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 bound.d 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r3
 bound.d 0xffffffff,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 bound.d 0x5432f789,r3
 test_move_cc 0 0 0 0
 checkr3 5432f789

 moveq -1,r3
 bound.w 2,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq -1,r3
 bound.w 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq 2,r3
 bound.w 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 bound.w 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 move.d 0xfedaffff,r3
 bound.w 0xffff,r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 move.d 0x78134452,r3
 bound.w 0xf789,r3
 test_move_cc 0 0 0 0
 checkr3 f789

 moveq -1,r3
 bound.b 2,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 bound.b 0xff,r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq -1,r3
 bound.b 0xff,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 move.d 0xff,r3
 bound.b 0xff,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 move.d 0xfeda49ff,r3
 bound.b 0xff,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 move.d 0x78134452,r3
 bound.b 0x89,r3
 test_move_cc 0 0 0 0
 checkr3 89

 bound.w 0,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0xffff,r3
 bound.b -1,r3
 test_move_cc 0 0 0 0
 checkr3 ff

 quit
