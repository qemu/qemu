# mach: crisv3 crisv8 crisv10 crisv32
# output: 42\n85\n7685\n8765\n0\n

; Move constant byte, word, dword to register.  Check that zero-extension
; is performed.

 .include "testutils.inc"
 start
 moveq -1,r3
 movu.b 0x42,r3
 test_move_cc 0 0 0 0
 checkr3 42

 moveq -1,r3
 movu.b 0x85,r3
 test_move_cc 0 0 0 0
 checkr3 85

 moveq -1,r3
 movu.w 0x7685,r3
 test_move_cc 0 0 0 0
 checkr3 7685

 moveq -1,r3
 movu.w 0x8765,r3
 test_move_cc 0 0 0 0
 checkr3 8765

 movu.b 0,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit
