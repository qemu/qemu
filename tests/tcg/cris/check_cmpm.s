# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\n2\nffff\nffffffff\n78134452\nffffffff\n2\nffff\nfedaffff\n78134452\nffffffff\n2\nff\nfeda49ff\n78134452\n85649222\n

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
 cmp.d [r5+],r3
 test_cc 0 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 cmp.d [r5],r3
 test_cc 0 0 0 0
 addq 4,r5
 checkr3 2

 move.d 0xffff,r3
 cmp.d [r5+],r3
 test_cc 0 0 0 1
 checkr3 ffff

 moveq -1,r3
 cmp.d [r5+],r3
 test_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x78134452,r3
 cmp.d [r5+],r3
 test_cc 1 0 1 1
 checkr3 78134452

 moveq -1,r3
 cmp.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 cmp.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 2

 move.d 0xffff,r3
 cmp.w [r5],r3
 test_cc 1 0 0 0
 checkr3 ffff

 move.d 0xfedaffff,r3
 cmp.w [r5+],r3
 test_cc 1 0 0 0
 checkr3 fedaffff

 move.d 0x78134452,r3
 cmp.w [r5+],r3
 test_cc 0 0 0 0
 checkr3 78134452

 moveq -1,r3
 cmp.b [r5],r3
 test_cc 0 0 0 0
 addq 1,r5
 checkr3 ffffffff

 moveq 2,r3
 cmp.b [r5],r3
 test_cc 0 0 0 0
 checkr3 2

 move.d 0xff,r3
 cmp.b [r5],r3
 test_cc 1 0 0 0
 checkr3 ff

 move.d 0xfeda49ff,r3
 cmp.b [r5+],r3
 test_cc 1 0 0 0
 checkr3 feda49ff

 move.d 0x78134452,r3
 cmp.b [r5+],r3
 test_cc 1 0 0 1
 checkr3 78134452

 move.d 0x85649222,r3
 cmp.b [r5],r3
 test_cc 0 1 0 0
 checkr3 85649222

 quit
