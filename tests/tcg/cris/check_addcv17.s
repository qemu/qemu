# mach:  crisv17

 .include "testutils.inc"

 .macro addc Rs Rd inc=0
# Create the instruction manually since there is no assembler support yet
 .word (\Rd << 12) | \Rs | (\inc << 10) | 0x09a0
 .endm

 start

 .data
mem1:
 .dword 0x0
mem2:
 .dword 0x12345678

 .text
 move.d mem1,r4
 clearf nzvc
 addc 4 3
 test_cc 0 1 0 0
 checkr3 0

 move.d mem1,r4
 clearf nzvc
 ax
 addc 4 3
 test_cc 0 0 0 0
 checkr3 0

 move.d mem1,r4
 clearf nzvc
 setf c
 addc 4 3
 test_cc 0 0 0 0
 checkr3 1

 move.d mem2,r4
 moveq 2, r3
 clearf nzvc
 setf c
 addc 4 3
 test_cc 0 0 0 0
 checkr3 1234567b

 move.d mem2,r5
 clearf nzvc
 cmp.d r4,r5
 test_cc 0 1 0 0

 move.d mem2,r4
 moveq 2, r3
 clearf nzvc
 addc 4 3 inc=1
 test_cc 0 0 0 0
 checkr3 1234567a

 move.d mem2,r5
 clearf nzvc
 addq 4,r5
 cmp.d r4,r5
 test_cc 0 1 0 0

 quit
