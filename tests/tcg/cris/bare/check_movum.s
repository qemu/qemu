# mach: crisv3 crisv8 crisv10 crisv32
# output: 5\nf5\n5\nfff5\n0\n

; Movu between registers.  Check that zero-extension is performed and the
; full register is set.

 .include "testutils.inc"

 .data
x:
 .byte 5,-11
 .word 5,-11
 .word 0

 start
 move.d x,r5

 movu.b [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 5

 movu.b [r5],r3
 test_move_cc 0 0 0 0
 addq 1,r5
 checkr3 f5

 movu.w [r5+],r3
 test_move_cc 0 0 0 0
 checkr3 5

 movu.w [r5],r3
 test_move_cc 0 0 0 0
 addq 2,r5
 checkr3 fff5

 movu.w [r5],r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
