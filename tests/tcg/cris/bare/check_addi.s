# mach:  crisv0 crisv3 crisv8 crisv10 crisv32
# output: 0\n1\n2\n4\nbe02460f\n69d035a6\nc16c14d4\n

 .include "testutils.inc"
 start
 moveq 0,r3
 moveq 0,r4
 clearf zcvn
 addi r4.b,r3
 test_cc 0 0 0 0
 checkr3 0

 moveq 0,r3
 moveq 1,r4
 setf zcvn
 addi r4.b,r3
 test_cc 1 1 1 1
 checkr3 1

 moveq 0,r3
 moveq 1,r4
 setf cv
 clearf zn
 addi r4.w,r3
 test_cc 0 0 1 1
 checkr3 2

 moveq 0,r3
 moveq 1,r4
 clearf cv
 setf zn
 addi r4.d,r3
 test_cc 1 1 0 0
 checkr3 4

 move.d 0x12345678,r3
 move.d 0xabcdef97,r4
 clearf cn
 setf zv
 addi r4.b,r3
 test_cc 0 1 1 0
 checkr3 be02460f

 move.d 0x12345678,r3
 move.d 0xabcdef97,r4
 setf cn
 clearf zv
 addi r4.w,r3
 test_cc 1 0 0 1
 checkr3 69d035a6

 move.d 0x12345678,r3
 move.d 0xabcdef97,r4
 addi r4.d,r3
 checkr3 c16c14d4

 quit
