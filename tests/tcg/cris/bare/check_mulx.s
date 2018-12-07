# mach: crisv10 crisv32
# output: fffffffe\nffffffff\nfffffffe\n1\nfffffffe\nffffffff\nfffffffe\n1\nfffe0001\n0\nfffe0001\n0\n1\n0\n1\nfffffffe\n193eade2\n277e3a49\n193eade2\n277e3a49\nfffffffe\nffffffff\n1fffe\n0\nfffffffe\nffffffff\n1fffe\n0\n1\n0\nfffe0001\n0\nfdbdade2\nffffffff\n420fade2\n0\nfffffffe\nffffffff\n1fe\n0\nfffffffe\nffffffff\n1fe\n0\n1\n0\nfe01\n0\n1\n0\nfe01\n0\nffffd9e2\nffffffff\n2be2\n0\n0\n0\n0\n0\n

 .include "testutils.inc"
 start
 moveq -1,r3
 moveq 2,r4
 muls.d r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq -1,r3
 moveq 2,r4
 mulu.d r4,r3
 test_cc 0 0 1 0
 checkr3 fffffffe
 move mof,r3
 checkr3 1

 moveq 2,r3
 moveq -1,r4
 muls.d r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq 2,r3
 moveq -1,r4
 mulu.d r4,r3
 test_cc 0 0 1 0
 checkr3 fffffffe
 move mof,r3
 checkr3 1

 move.d 0xffff,r4
 move.d r4,r3
 muls.d r4,r3
 test_cc 0 0 1 0
 checkr3 fffe0001
 move mof,r3
 checkr3 0

 move.d 0xffff,r4
 move.d r4,r3
 mulu.d r4,r3
 test_cc 0 0 0 0
 checkr3 fffe0001
 move mof,r3
 checkr3 0

 moveq -1,r4
 move.d r4,r3
 muls.d r4,r3
 test_cc 0 0 0 0
 checkr3 1
 move mof,r3
 checkr3 0

 moveq -1,r4
 move.d r4,r3
 mulu.d r4,r3
 test_cc 1 0 1 0
 checkr3 1
 move mof,r3
 checkr3 fffffffe

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 muls.d r4,r3
 test_cc 0 0 1 0
 checkr3 193eade2
 move mof,r3
 checkr3 277e3a49

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 mulu.d r4,r3
 test_cc 0 0 1 0
 checkr3 193eade2
 move mof,r3
 checkr3 277e3a49

 move.d 0xffff,r3
 moveq 2,r4
 muls.w r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq -1,r3
 moveq 2,r4
 mulu.w r4,r3
 test_cc 0 0 0 0
 checkr3 1fffe
 move mof,r3
 checkr3 0

 moveq 2,r3
 move.d 0xffff,r4
 muls.w r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq 2,r3
 moveq -1,r4
 mulu.w r4,r3
 test_cc 0 0 0 0
 checkr3 1fffe
 move mof,r3
 checkr3 0

 move.d 0xffff,r4
 move.d r4,r3
 muls.w r4,r3
 test_cc 0 0 0 0
 checkr3 1
 move mof,r3
 checkr3 0

 moveq -1,r4
 move.d r4,r3
 mulu.w r4,r3
 test_cc 0 0 0 0
 checkr3 fffe0001
 move mof,r3
 checkr3 0

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 muls.w r4,r3
 test_cc 1 0 0 0
 checkr3 fdbdade2
 move mof,r3
 checkr3 ffffffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 mulu.w r4,r3
 test_cc 0 0 0 0
 checkr3 420fade2
 move mof,r3
 checkr3 0

 move.d 0xff,r3
 moveq 2,r4
 muls.b r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq -1,r3
 moveq 2,r4
 mulu.b r4,r3
 test_cc 0 0 0 0
 checkr3 1fe
 move mof,r3
 checkr3 0

 moveq 2,r3
 moveq -1,r4
 muls.b r4,r3
 test_cc 1 0 0 0
 checkr3 fffffffe
 move mof,r3
 checkr3 ffffffff

 moveq 2,r3
 moveq -1,r4
 mulu.b r4,r3
 test_cc 0 0 0 0
 checkr3 1fe
 move mof,r3
 checkr3 0

 move.d 0xff,r4
 move.d r4,r3
 muls.b r4,r3
 test_cc 0 0 0 0
 checkr3 1
 move mof,r3
 checkr3 0

 moveq -1,r4
 move.d r4,r3
 mulu.b r4,r3
 test_cc 0 0 0 0
 checkr3 fe01
 move mof,r3
 checkr3 0

 move.d 0xfeda49ff,r4
 move.d r4,r3
 muls.b r4,r3
 test_cc 0 0 0 0
 checkr3 1
 move mof,r3
 checkr3 0

 move.d 0xfeda49ff,r4
 move.d r4,r3
 mulu.b r4,r3
 test_cc 0 0 0 0
 checkr3 fe01
 move mof,r3
 checkr3 0

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 muls.b r4,r3
 test_cc 1 0 0 0
 checkr3 ffffd9e2
 move mof,r3
 checkr3 ffffffff

 move.d 0x5432f789,r4
 move.d 0x78134452,r3
 mulu.b r4,r3
 test_cc 0 0 0 0
 checkr3 2be2
 move mof,r3
 checkr3 0

 moveq 0,r3
 move.d 0xf87f4aeb,r4
 muls.d r4,r3
 test_cc 0 1 0 0
 checkr3 0
 move mof,r3
 checkr3 0

 move.d 0xf87f4aeb,r3
 moveq 0,r4
 mulu.d r4,r3
 test_cc 0 1 0 0
 checkr3 0
 move mof,r3
 checkr3 0

 quit
