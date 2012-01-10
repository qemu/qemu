# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: 1\n1\n1fffe\nfffffffe\ncc463bdb\nffff0001\n1\nfffe\nfedafffe\n78133bdb\nffffff01\n1\nfe\nfeda49fe\n781344db\n85649200\n

 .include "testutils.inc"
 .data
x:
 .dword -2,1,-0xffff,1,-0x5432f789
 .word -2,1,1,0x877
 .byte -2,1,0x77
 .byte 0x22

 start
 moveq -1,r3
 move.d x,r5
 sub.d [r5+],r3
 test_cc 0 0 0 0
 checkr3 1

 moveq 2,r3
 sub.d [r5],r3
 test_cc 0 0 0 0
 addq 4,r5
 checkr3 1

 move.d 0xffff,r3
 sub.d [r5+],r3
 test_cc 0 0 0 1
 checkr3 1fffe

 moveq -1,r3
 sub.d [r5+],r3
 test_cc 1 0 0 0
 checkr3 fffffffe

 move.d 0x78134452,r3
 sub.d [r5+],r3
 test_cc 1 0 1 1
 checkr3 cc463bdb

 moveq -1,r3
 sub.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 ffff0001

 moveq 2,r3
 sub.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 1

 move.d 0xffff,r3
 sub.w [r5],r3
 test_cc 1 0 0 0
 checkr3 fffe

 move.d 0xfedaffff,r3
 sub.w [r5+],r3
 test_cc 1 0 0 0
 checkr3 fedafffe

 move.d 0x78134452,r3
 sub.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 78133bdb

 moveq -1,r3
 sub.b [r5],r3
 test_cc 0 0 0 0
 addq 1,r5
 checkr3 ffffff01

 moveq 2,r3
 sub.b [r5],r3
 test_cc 0 0 0 0
 checkr3 1

 move.d 0xff,r3
 sub.b [r5],r3
 test_cc 1 0 0 0
 checkr3 fe

 move.d 0xfeda49ff,r3
 sub.b [r5+],r3
 test_cc 1 0 0 0
 checkr3 feda49fe

 move.d 0x78134452,r3
 sub.b [r5+],r3
 test_cc 1 0 0 1
 checkr3 781344db

 move.d 0x85649222,r3
 sub.b [r5],r3
 test_cc 0 1 0 0
 checkr3 85649200

 quit
