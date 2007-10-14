# mach: crisv32
# output: ffffff20\nbb113344\n

# Test v32-specific special registers.  FIXME: more registers.

 .include "testutils.inc"
 start
 moveq -1,r3
 setf zcvn
 move vr,r3
 test_cc 1 1 1 1
 checkr3 ffffff20

 moveq -1,r3
 move.d 0xbb113344,r4
 clearf cvnz
 move r4,mof
 test_cc 0 0 0 0
 move mof,r3
 checkr3 bb113344
 quit
