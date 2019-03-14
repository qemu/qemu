# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 3\n3\nffff\nffffffff\n7c33f7db\nffff0003\n3\nfedaffff\n7813f7db\n3\n3\nfeb\n781344db\n

 .include "testutils.inc"
 .data
x:
 .dword 2,1,0xff0f,-1,0x5432f789
 .word 2,1,0xff5f,0xf789
 .byte 2,1,0x4a,0x89

 start
 moveq 1,r3
 move.d x,r5
 or.d [r5+],r3
 checkr3 3

 moveq 2,r3
 or.d [r5],r3
 addq 4,r5
 checkr3 3

 move.d 0xf0ff,r3
 or.d [r5+],r3
 checkr3 ffff

 moveq -1,r3
 or.d [r5+],r3
 checkr3 ffffffff

 move.d 0x78134452,r3
 or.d [r5+],r3
 checkr3 7c33f7db

 move.d 0xffff0001,r3
 or.w [r5+],r3
 checkr3 ffff0003

 moveq 2,r3
 or.w [r5],r3
 addq 2,r5
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0xfedaffaf,r3
 or.w [r5+],r3
 test_move_cc 1 0 0 0
 checkr3 fedaffff

 move.d 0x78134452,r3
 or.w [r5+],r3
 test_move_cc 1 0 0 0
 checkr3 7813f7db

 moveq 1,r3
 or.b [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 3

 moveq 2,r3
 or.b [r5],r3
 addq 1,r5
 test_move_cc 0 0 0 0
 checkr3 3

 move.d 0xfa3,r3
 or.b [r5+],r3
 test_move_cc 1 0 0 0
 checkr3 feb

 move.d 0x78134453,r3
 or.b [r5],r3
 test_move_cc 1 0 0 0
 checkr3 781344db

 quit
