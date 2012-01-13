# mach: crisv32
# output: 0\n0\nfffffffa\nfffffffe\nffffffda\n1e\n1e\n0\n

.include "testutils.inc"

; To accommodate dumpr3 with more than one instruction, keep it
; out of lapc operand ranges and difference calculations.

 start
 lapc.d 0f,r3
0:
 sub.d .,r3
 checkr3 0

 lapcq 0f,r3
0:
 sub.d .,r3
 checkr3 0

 lapc.d .,r3
 sub.d .,r3
 checkr3 fffffffa

 lapcq .,r3
 sub.d .,r3
 checkr3 fffffffe

0:
 .rept 16
 nop
 .endr
 lapc.d 0b,r3
 sub.d .,r3
 checkr3 ffffffda

 setf zcvn
 lapc.d 0f,r3
 test_cc 1 1 1 1
 sub.d .,r3
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
0:
 checkr3 1e
0:
 lapcq 0f,r3
 sub.d 0b,r3
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
 nop
0:
 checkr3 1e
 clearf cn
 setf zv
1:
 lapcq .,r3
 test_cc 0 1 1 0
 sub.d 1b,r3
 checkr3 0

 quit
