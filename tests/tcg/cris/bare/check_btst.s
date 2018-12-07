# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 1111\n

 .include "testutils.inc"
 start
 clearf nzvc
 moveq -1,r3
 .if 1 ;..asm.arch.cris.v32
 .else
 setf vc
 .endif
 btstq 0,r3
 test_cc 1 0 0 0

 moveq 2,r3
 btstq 1,r3
 test_cc 1 0 0 0

 moveq 4,r3
 btstq 1,r3
 test_cc 0 1 0 0

 moveq -1,r3
 btstq 31,r3
 test_cc 1 0 0 0

 move.d 0x5a67f19f,r3
 btstq 12,r3
 test_cc 1 0 0 0

 move.d 0xda67f19f,r3
 move.d 29,r4
 btst r4,r3
 test_cc 0 0 0 0

 move.d 0xda67f19f,r3
 move.d 32,r4
 btst r4,r3
 test_cc 1 0 0 0

 move.d 0xda67f191,r3
 move.d 33,r4
 btst r4,r3
 test_cc 0 0 0 0

 moveq -1,r3
 moveq 0,r4
 btst r4,r3
 test_cc 1 0 0 0

 moveq 2,r3
 moveq 1,r4
 btst r4,r3
 test_cc 1 0 0 0

 moveq -1,r3
 moveq 31,r4
 btst r4,r3
 test_cc 1 0 0 0

 moveq 4,r3
 btstq 1,r3
 test_cc 0 1 0 0

 moveq -1,r3
 moveq 15,r4
 btst r4,r3
 test_cc 1 0 0 0

 move.d 0x5a67f19f,r3
 moveq 12,r4
 btst r4,r3
 test_cc 1 0 0 0

 move.d 0x5a678000,r3
 moveq 11,r4
 btst r4,r3
 test_cc 0 1 0 0

 move.d 0x5a67f19f,r3
 btst r3,r3
 test_cc 0 0 0 0

 move.d 0x1111,r3
 checkr3 1111

 ; check that X gets cleared and that only the NZ flags are touched.
 move.d	0xff, $r0
 move $r0, $ccs
 btst r3,r3
 move $ccs, $r0
 and.d 0xff, $r0
 cmp.d	0xe3, $r0
 test_cc 0 1 0 0

 quit
