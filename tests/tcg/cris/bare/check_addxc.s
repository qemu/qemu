# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 1\n1\n101\n10001\n100fe\n1fffe\nfffe\nfffe\nfffffffe\nfe\nfffffffe\n781344db\n781343db\n78143bdb\n78133bdb\n800000ed\n0\n

 .include "testutils.inc"
 start
 moveq 2,r3
 adds.b 0xff,r3
 test_cc 0 0 0 1
 checkr3 1

 moveq 2,r3
 adds.w 0xffff,r3
 test_cc 0 0 0 1
 checkr3 1

 moveq 2,r3
 addu.b 0xff,r3
 checkr3 101

 moveq 2,r3
 move.d 0xffffffff,r4
 addu.w -1,r3
 test_cc 0 0 0 0
 checkr3 10001

 move.d 0xffff,r3
 addu.b -1,r3
 test_cc 0 0 0 0
 checkr3 100fe

 move.d 0xffff,r3
 addu.w -1,r3
 test_cc 0 0 0 0
 checkr3 1fffe

 move.d 0xffff,r3
 adds.b 0xff,r3
 test_cc 0 0 0 1
 checkr3 fffe

 move.d 0xffff,r3
 adds.w 0xffff,r3
 test_cc 0 0 0 1
 checkr3 fffe

 moveq -1,r3
 adds.b 0xff,r3
 test_cc 1 0 0 1
 checkr3 fffffffe

 moveq -1,r3
 adds.w 0xff,r3
 test_cc 0 0 0 1
 checkr3 fe

 moveq -1,r3
 adds.w 0xffff,r3
 test_cc 1 0 0 1
 checkr3 fffffffe

 move.d 0x78134452,r3
 addu.b 0x89,r3
 test_cc 0 0 0 0
 checkr3 781344db

 move.d 0x78134452,r3
 adds.b 0x89,r3
 test_cc 0 0 0 1
 checkr3 781343db

 move.d 0x78134452,r3
 addu.w 0xf789,r3
 test_cc 0 0 0 0
 checkr3 78143bdb

 move.d 0x78134452,r3
 adds.w 0xf789,r3
 test_cc 0 0 0 1
 checkr3 78133bdb

 move.d 0x7fffffee,r3
 addu.b 0xff,r3
 test_cc 1 0 1 0
 checkr3 800000ed

 move.d 0x1,r3
 adds.w 0xffff,r3
 test_cc 0 1 0 1
 checkr3 0

 quit
