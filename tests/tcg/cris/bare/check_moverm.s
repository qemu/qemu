# mach: crisv3 crisv8 crisv10 crisv32
# output: 7823fec2\n10231879\n102318fe\n

 .include "testutils.inc"
 start

 .data
mem1:
 .dword 0x12345678
mem2:
 .word 0x4567
mem3:
 .byte 0x23
 .dword 0x76543210
 .byte 0xaa,0x11,0x99

 .text
 move.d mem1,r2
 move.d 0x7823fec2,r4
 setf nzvc
 move.d r4,[r2+]
 test_cc 1 1 1 1
 subq 4,r2
 move.d [r2],r3
 checkr3 7823fec2

 move.d mem2,r3
 move.d 0x45231879,r4
 clearf nzvc
 move.w r4,[r3]
 test_cc 0 0 0 0
 move.d [r3],r3
 checkr3 10231879

 move.d mem2,r2
 moveq -2,r4
 clearf nc
 setf zv
 move.b r4,[r2+]
 test_cc 0 1 1 0
 subq 1,r2
 move.d [r2],r3
 checkr3 102318fe

 quit
