# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\n2\nffff\nffffffff\n78134452\nffffffff\n2\nffff\nfedaffff\n78134452\nffffffff\n2\nff\nfeda49ff\n78134452\n85649282\n

 .include "testutils.inc"
 start
 moveq -1,r3
 cmp.d -2,r3
 test_cc 0 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 cmp.d 1,r3
 test_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 cmp.d -0xffff,r3
 test_cc 0 0 0 1
 checkr3 ffff

 moveq -1,r3
 cmp.d 1,r3
 test_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 cmp.d -0x5432f789,r3
 test_cc 1 0 1 1
 checkr3 78134452

 moveq -1,r3
 cmp.w -2,r3
 test_cc 0 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 cmp.w 1,r3
 test_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 cmp.w 1,r3
 test_cc 1 0 0 0
 checkr3 ffff

 move.d 0xfedaffff,r3
 cmp.w 1,r3
 test_cc 1 0 0 0
 checkr3 fedaffff

 move.d 0x78134452,r3
 cmp.w 0x877,r3
 test_cc 0 0 0 0
 checkr3 78134452

 moveq -1,r3
 cmp.b -2,r3
 test_cc 0 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 cmp.b 1,r3
 test_cc 0 0 0 0
 checkr3 2

 move.d 0xff,r3
 cmp.b 1,r3
 test_cc 1 0 0 0
 checkr3 ff

 move.d 0xfeda49ff,r3
 cmp.b 1,r3
 test_cc 1 0 0 0
 checkr3 feda49ff

 move.d 0x78134452,r3
 cmp.b 0x77,r3
 test_cc 1 0 0 1
 checkr3 78134452

 move.d 0x85649282,r3
 cmp.b 0x82,r3
 test_cc 0 1 0 0
 checkr3 85649282

 quit
