# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\n2\n2\nffff\nffff\nffff\nffff\nffffffff\nffffffff\nffffffff\n78134452\n78134452\n78134452\n78134452\n4452\n80000032\n

 .include "testutils.inc"
 start
 moveq 2,r3
 cmps.b 0xff,r3
 test_cc 0 0 0 1
 checkr3 2

 moveq 2,r3
 cmps.w 0xffff,r3
 test_cc 0 0 0 1
 checkr3 2

 moveq 2,r3
 cmpu.b 0xff,r3
 test_cc 1 0 0 1
 checkr3 2

 moveq 2,r3
 move.d 0xffffffff,r4
 cmpu.w -1,r3
 test_cc 1 0 0 1
 checkr3 2

 move.d 0xffff,r3
 cmpu.b -1,r3
 test_cc 0 0 0 0
 checkr3 ffff

 move.d 0xffff,r3
 cmpu.w -1,r3
 test_cc 0 1 0 0
 checkr3 ffff

 move.d 0xffff,r3
 cmps.b 0xff,r3
 test_cc 0 0 0 1
 checkr3 ffff

 move.d 0xffff,r3
 cmps.w 0xffff,r3
 test_cc 0 0 0 1
 checkr3 ffff

 moveq -1,r3
 cmps.b 0xff,r3
 test_cc 0 1 0 0
 checkr3 ffffffff

 moveq -1,r3
 cmps.w 0xff,r3
 test_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 cmps.w 0xffff,r3
 test_cc 0 1 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 cmpu.b 0x89,r3
 test_cc 0 0 0 0
 checkr3 78134452

 move.d 0x78134452,r3
 cmps.b 0x89,r3
 test_cc 0 0 0 1
 checkr3 78134452

 move.d 0x78134452,r3
 cmpu.w 0xf789,r3
 test_cc 0 0 0 0
 checkr3 78134452

 move.d 0x78134452,r3
 cmps.w 0xf789,r3
 test_cc 0 0 0 1
 checkr3 78134452

 move.d 0x4452,r3
 cmps.w 0x8002,r3
 test_cc 0 0 0 1
 checkr3 4452

 move.d 0x80000032,r3
 cmpu.w 0x764,r3
 test_cc 0 0 1 0
 checkr3 80000032

 quit
