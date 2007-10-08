# mach: crisv3 crisv8 crisv10 crisv32
# output: 1\n1\n1\n1f\n1f\nffffffe1\nffffffe1\nffffffe0\n0\n0\nffffffff\nffffffff\n10000\n100\n5678900\n

 .include "testutils.inc"
 start
 moveq 1,r3
 cmpq 1,r3
 test_cc 0 1 0 0
 checkr3 1

 cmpq -1,r3
 test_cc 0 0 0 1
 checkr3 1

 cmpq 31,r3
 test_cc 1 0 0 1
 checkr3 1

 moveq 31,r3
 cmpq 31,r3
 test_cc 0 1 0 0
 checkr3 1f

 cmpq -31,r3
 test_cc 0 0 0 1
 checkr3 1f

 movs.b -31,r3
 cmpq -31,r3
 test_cc 0 1 0 0
 checkr3 ffffffe1

 cmpq -32,r3
 test_cc 0 0 0 0
 checkr3 ffffffe1

 movs.b -32,r3
 cmpq -32,r3
 test_cc 0 1 0 0
 checkr3 ffffffe0

 moveq 0,r3
 cmpq 1,r3
 test_cc 1 0 0 1
 checkr3 0

 cmpq -32,r3
 test_cc 0 0 0 1
 checkr3 0

 moveq -1,r3
 cmpq 1,r3
 test_cc 1 0 0 0
 checkr3 ffffffff

 cmpq -1,r3
 test_cc 0 1 0 0
 checkr3 ffffffff

 move.d 0x10000,r3
 cmpq 1,r3
 test_cc 0 0 0 0
 checkr3 10000

 move.d 0x100,r3
 cmpq 1,r3
 test_cc 0 0 0 0
 checkr3 100

 move.d 0x5678900,r3
 cmpq 7,r3
 test_cc 0 0 0 0
 checkr3 5678900

 quit
