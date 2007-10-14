# mach: crisv3 crisv8 crisv10 crisv32
# output: 12345678\n10234567\n12345678\n12344567\n12344523\n76543210\nffffffaa\naa\n9911\nffff9911\n78\n56\n3456\n6712\n

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
 move.d [r2],r3
 test_move_cc 0 0 0 0
 checkr3 12345678

 move.d mem2,r3
 move.d [r3],r3
 test_move_cc 0 0 0 0
 checkr3 10234567

 move.d mem1,r2
 move.d [r2+],r3
 test_move_cc 0 0 0 0
 checkr3 12345678

 move.w [r2+],r3
 test_move_cc 0 0 0 0
 checkr3 12344567

 move.b [r2+],r3
 test_move_cc 0 0 0 0
 checkr3 12344523

 move.d [r2+],r3
 test_move_cc 0 0 0 0
 checkr3 76543210

 movs.b [r2],r3
 test_move_cc 1 0 0 0
 checkr3 ffffffaa

 movu.b [r2+],r3
 test_move_cc 0 0 0 0
 checkr3 aa

 movu.w [r2],r3
 test_move_cc 0 0 0 0
 checkr3 9911

 movs.w [r2+],r3
 test_move_cc 1 0 0 0
 checkr3 ffff9911

 move.d mem1,r13
 movs.b [r13+],r3
 test_move_cc 0 0 0 0
 checkr3 78

 movu.b [r13],r3
 test_move_cc 0 0 0 0
 checkr3 56

 movs.w [r13+],r3
 test_move_cc 0 0 0 0
 checkr3 3456

 movu.w [r13+],r3
 test_move_cc 0 0 0 0
 checkr3 6712

 quit

