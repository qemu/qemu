# mach: crisv32
# output: 11223320\nbb113344\naa557711\n

# Test v32-specific special registers.  FIXME: more registers.

 .include "testutils.inc"
 start
 .data
store:
 .dword 0x11223344
 .dword 0x77665544

 .text
 moveq -1,r3
 move.d store,r4
 move vr,[r4]
 move [r4+],mof
 move mof,r3
 checkr3 11223320

 moveq -1,r3
 clearf zcvn
 move 0xbb113344,mof
 test_cc 0 0 0 0
 move mof,r3
 checkr3 bb113344

 setf zcvn
 move 0xaa557711,mof
 test_cc 1 1 1 1
 move mof,[r4]
 move.d [r4],r3
 checkr3 aa557711

 quit
