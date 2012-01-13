# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 2\n2\nffff\nffffffff\n50124400\nffff0002\n2\nfffff\nfedaff0f\n78134400\nffffff02\n2\nf02\n78134401\n78134400\n

 .include "testutils.inc"
 .data
x:
 .dword 2,-1,0xffff,-1,0x5432f789
 .word 2,-1,0xffff,0xff5f,0xf789
 .byte 2,-1,0x5a,0x89,0

 start
 moveq -1,r3
 move.d x,r5
 and.d [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 2

 moveq 2,r3
 and.d [r5],r3
 test_move_cc 0 0 0 0
 addq 4,r5
 checkr3 2

 move.d 0xffff,r3
 and.d [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 ffff

 moveq -1,r3
 and.d [r5+],r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 and.d [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 50124400

 moveq -1,r3
 and.w [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 ffff0002

 moveq 2,r3
 and.w [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xfffff,r3
 and.w [r5],r3
 test_move_cc 1 0 0 0
 addq 2,r5
 checkr3 fffff

 move.d 0xfedaffaf,r3
 and.w [r5+],r3
 test_move_cc 1 0 0 0
 checkr3 fedaff0f

 move.d 0x78134452,r3
 and.w [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 78134400

 moveq -1,r3
 and.b [r5],r3
 test_move_cc 0 0 0 0
 addq 1,r5
 checkr3 ffffff02

 moveq 2,r3
 and.b [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 2

 move.d 0xfa7,r3
 and.b [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 f02

 move.d 0x78134453,r3
 and.b [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 78134401

 and.b [r5],r3
 test_move_cc 0 1 0 0
 checkr3 78134400

 quit
